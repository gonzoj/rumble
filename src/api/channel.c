#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>

#include "channel.h"
#include "../client.h"
#include "../console.h"
#include "environment.h"
#include "../list.h"
#include "../plugin.h"
#include "../net/protobuf/Mumble.pb-c.h"
#include "../types.h"

#define _CLASS "channel"

static struct interface api[] = {
	API_PACKAGE("Channel"),
	API_FUNCTION("exists", lua_channel_exists),
	API_FUNCTION("create", lua_channel_create),
	API_FUNCTION("remove", lua_channel_remove),
	API_FUNCTION("join", lua_channel_join),
	API_FUNCTION("sendTextMessage", lua_channel_send_text_message),
	API_PACKAGE_END
};

struct channel * channel_get_by_id(uint32_t id, struct list_head *channels) {
	struct channel *c;
	list_for_each_entry(c, channels, l_channels) {
		if (c->id == id) return c;
	}
	
	return NULL;
}

bool channel_is_valid(struct channel *c, struct list_head *channels) {
	struct channel *_c;
	list_for_each_entry(_c, channels, l_channels) {
		if (_c == c) return TRUE;
	}

	return FALSE;
}

void channel_free(struct channel *c) {
	if (c->name) free(c->name);

	if (c->description) free(c->description);

	list_del(&c->l_channels);

	free(c);
}

bool channel_exists(struct client *c, struct channel *parent, const char *name) {
	struct channel *_c;
	list_for_each_entry(_c, &c->env->channels, l_channels) {
		if (!strcmp(_c->name, name) && _c->parent == parent) return TRUE;
	}

	return FALSE;
}

int lua_channel_exists(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_channel);

	struct channel *parent = lua_touserdata(L,1);
	const char *name = luaL_checkstring(L, 2);

	lua_pushboolean(L, channel_exists(env->client, parent, name));

	pthread_mutex_unlock(&env->m_channel);

	return 1;
}

void channel_create(struct client *c, struct channel *parent, const char *name, const char *description, bool temporary) {
	MumbleProto__ChannelState msg = message_new(CHANNEL_STATE);

	message_set_optional(msg, parent, parent->id);
	msg.name = (char *) name;
	msg.description = (char *) description;
	message_set_optional(msg, temporary, temporary);

	message_send(c->con, m_CHANNEL_STATE, &msg);
}

int lua_channel_create(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_channel);

	struct channel *parent = lua_touserdata(L, 1);
	if (!channel_is_valid(parent, &env->channels)) goto exit;

	const char *name = luaL_checkstring(L, 2);
	if (!name) goto exit;

	const char *desc = luaL_checkstring(L, 3);
	if (!desc) goto exit;

	bool temp = lua_toboolean(L, 4);

	channel_create(env->client, parent, name, desc, temp);

	exit:
	pthread_mutex_unlock(&env->m_channel);

	return 0;
}

void channel_remove(struct client *c, struct channel *cn) {
	MumbleProto__ChannelRemove msg = message_new(CHANNEL_REMOVE);

	msg.channel_id = cn->id;

	message_send(c->con, m_CHANNEL_REMOVE, &msg);
}

int lua_channel_remove(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_channel);

	struct channel *c = lua_touserdata(L, 1);
	if (!channel_is_valid(c, &env->channels)) goto exit;

	channel_remove(env->client, c);

	exit:
	pthread_mutex_unlock(&env->m_channel);

	return 0;
}

void channel_join(struct client *c, struct channel *cn) {
	MumbleProto__UserState msg = message_new(USER_STATE);

	message_set_optional(msg, session, c->session);
	message_set_optional(msg, channel_id, cn->id);

	message_send(c->con, m_USER_STATE, &msg);
}

int lua_channel_join(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_channel);

	struct channel *c = lua_touserdata(L, 1);
	if (!channel_is_valid(c, &env->channels)) goto exit;

	channel_join(env->client, c);

	exit:
	pthread_mutex_unlock(&env->m_channel);

	return 0;
}

void channel_send_text_message(struct client *c, struct channel *cn, const char *format, ...) {
	MumbleProto__TextMessage msg = message_new(TEXT_MESSAGE);
	
	message_add_repeated(msg, channel_id, cn->id);

	char *message;

	va_list vl;
	va_start(vl, format);

	char short_message[1024];
	int len = vsnprintf(short_message, sizeof(short_message), format, vl);

	va_end(vl);

	char *long_message = NULL;

	if (len >= sizeof(short_message)) {
		va_start(vl, format);

		long_message = malloc(len + 1);
		vsnprintf(long_message, len + 1, format, vl);

		va_end(vl);

		message = long_message;
	} else {
		message = short_message;
	}

	msg.message = message;

	message_send(c->con, m_TEXT_MESSAGE, &msg);

	message_free_repeated(msg, channel_id);

	console_message(_CLASS, c->username, "to channel %s: %s\n", cn->name, message);

	if (long_message) free(long_message);
}

int lua_channel_send_text_message(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_channel);

	struct channel *cn = lua_touserdata(L, 1);
	if (!channel_is_valid(cn, &env->channels)) goto exit;

	const char *m = luaL_checkstring(L, 2);
	if (!m) goto exit;

	channel_send_text_message(env->client, cn, m);

	exit:
	pthread_mutex_unlock(&env->m_channel);

	return 0;
}

void channel_install(struct plugin *p) {
	interface_install(p, api);
}

void api_on_channel_state(struct client *c, MumbleProto__ChannelState *msg) {
	if (!msg->has_channel_id) return;

	struct channel *cn = channel_get_by_id(msg->channel_id, &c->env->channels);
	struct channel *p = msg->has_parent ? channel_get_by_id(msg->parent, &c->env->channels) : NULL;

	bool new = (cn == NULL);

	pthread_mutex_lock(&c->env->m_channel);

	if (new) {
		if (msg->channel_id == 0 || (msg->has_parent && p && msg->name)) {
			cn = malloc(sizeof(struct channel));
			cn->id = msg->channel_id;
			cn->name = NULL;
			cn->description = NULL;
			cn->temporary = msg->temporary;
			//INIT_LIST_HEAD(&cn->links);
			list_add_tail(&cn->l_channels, &c->env->channels);
		} else {		
			pthread_mutex_unlock(&c->env->m_channel);

			return;
		}
	}

	if (p) cn->parent = p;

	if (msg->name) {
		if (cn->name) free(cn->name);
		cn->name = strdup(msg->name);
	}

	if (msg->description) {
		if (cn->description) free(cn->description);
		cn->description = strdup(msg->description);
	}

	if (msg->has_position) {
		cn->position = msg->position;
	}

	pthread_mutex_unlock(&c->env->m_channel);

	/*int i;
	message_for_each_repeated(msg, links_add, i) {
		struct channel *l = channel_get_by_id(msg->links_add[i], &c->env->channels);
		if (l) {
			list_add_tail(&l->links, &cn->links);
		}
	}

	message_for_each_repeated(msg, links_remove, i) {
		struct channel *l = channel_get_by_id(msg->links_remove[i], &c->env->channels);
		if (l) {
			list_del(&l->links);
		}
	}*/
}

void api_on_channel_remove(struct client *c, MumbleProto__ChannelRemove *msg) {
	struct channel *cn;

	if (!(cn = channel_get_by_id(msg->channel_id, &c->env->channels))) return;

	pthread_mutex_lock(&c->env->m_channel);

	channel_free(cn);

	pthread_mutex_unlock(&c->env->m_channel);
}
