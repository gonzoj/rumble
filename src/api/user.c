#include <arpa/inet.h>
#include <lua.h>
#include <lauxlib.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "../client.h"
#include "../console.h"
#include "../controller.h"
#include "environment.h"
#include "event.h"
#include "../list.h"
#include "../net/protobuf/Mumble.pb-c.h"
#include "../plugin.h"
#include "../types.h"
#include "user.h"

#define _CLASS "user"

static struct interface api[] = {
	API_PACKAGE("User"),
	API_FUNCTION("getPrivilege", lua_user_get_privilege),
	API_FUNCTION("getByName", lua_user_get_by_name),
	API_FUNCTION("getChannel", lua_user_get_channel),
	API_FUNCTION("getName", lua_user_get_name),
	API_FUNCTION("getAddress", lua_user_get_address),
	API_FUNCTION("sendTextMessage", lua_user_send_text_message),
	API_FUNCTION("requestStats", lua_user_request_stats),
	API_PACKAGE_END
};

struct user * user_get_by_session(uint32_t session, struct list_head *users) {
	struct user *u;
	list_for_each_entry(u, users, l_users) {
		if (u->session == session) return u;
	}

	return NULL;
}

struct user * user_get_by_name(char *name, struct list_head *users) {
	struct user *u;
	list_for_each_entry(u, users, l_users) {
		if (!strcmp(u->name, name)) return u;
	}

	return NULL;
}

static bool user_is_valid(struct user *u, struct list_head *users) {
	struct user *_u;
	list_for_each_entry(_u, users, l_users) {
		if (_u == u) return TRUE;
	}

	return FALSE;
}

void user_free(struct user *u) {
	free(u->name);

	if (u->address) free(u->address);

	list_del(&u->l_users);

	free(u);
}

int user_get_privilege(struct client *c, struct user *u) {
	return controller_get_privilege(&c->privileges, u);
}

int lua_user_get_privilege(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_user);

	struct user *u = lua_touserdata(L, 1);
	if (!user_is_valid(u, &env->users)) goto exit;

	pthread_mutex_lock(&env->client->m_privilege);

	lua_pushnumber(L, user_get_privilege(env->client, u));

	pthread_mutex_unlock(&env->client->m_privilege);

	exit:
	pthread_mutex_unlock(&env->m_user);

	return 1;
}

int lua_user_get_by_name(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_user);

	const char *name = luaL_checkstring(L, 1);
	if (!name) {
		lua_pushnil(L);
		goto exit;
	}

	struct user *u = user_get_by_name(name, &env->users);
	if (u) {
		lua_pushlightuserdata(L, u);
	} else {
		lua_pushnil(L);
	}

	exit:
	pthread_mutex_unlock(&env->m_user);

	return 1;
}


int lua_user_get_channel(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_user);
	
	struct user *u = lua_touserdata(L, 1);
	if (!user_is_valid(u, &env->users)) goto exit;

	pthread_mutex_lock(&env->m_channel);

	lua_pushlightuserdata(L, u->channel);

	pthread_mutex_unlock(&env->m_channel);

	exit:
	pthread_mutex_unlock(&env->m_user);

	return 1;
}

int lua_user_get_name(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_user);
	
	struct user *u = lua_touserdata(L, 1);
	if (!user_is_valid(u, &env->users)) goto exit;

	lua_pushstring(L, u->name);

	exit:
	pthread_mutex_unlock(&env->m_user);

	return 1;
}

int lua_user_get_address(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_user);
	
	struct user *u = lua_touserdata(L, 1);
	if (!user_is_valid(u, &env->users)) goto exit;

	if (u->address) lua_pushstring(L, u->address);
	else lua_pushnil(L);

	exit:
	pthread_mutex_unlock(&env->m_user);

	return 1;
}


void user_send_text_message(struct client *c, struct user *u, const char *format, ...) {
	MumbleProto__TextMessage msg = message_new(TEXT_MESSAGE);
	
	message_add_repeated(msg, session, u->session);

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

	message_free_repeated(msg, session);

	console_message(_CLASS, c->username, "to %s: %s\n", u->name, message);

	if (long_message) free(long_message);
}

int lua_user_send_text_message(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_user);

	struct user *u = lua_touserdata(L, 1);
	if (!user_is_valid(u, &env->users)) goto exit;

	const char *m = luaL_checkstring(L, 2);
	if (!m) goto exit;

	user_send_text_message(env->client, u, m);

	exit:
	pthread_mutex_unlock(&env->m_user);

	return 0;
}

int lua_user_request_stats(lua_State *L) {
	struct environment *env = environment_get();

	pthread_mutex_lock(&env->m_user);

	struct user *u = lua_touserdata(L, 1);
	if (!user_is_valid(u, &env->users)) goto exit;

	MumbleProto__UserStats msg = message_new(USER_STATS);

	message_set_optional(msg, session, u->session);
	
	message_send(env->client->con, m_USER_STATS, &msg);

	exit:
	pthread_mutex_unlock(&env->m_user);

	return 0;
}

void user_install(struct plugin *p) {
	interface_install(p, api);

	lua_getglobal(p->L, api[0].name);
	lua_pushstring(p->L, "Privilege");
	controller_create_privileges(p->L);
	lua_settable(p->L, -3);
	lua_pop(p->L, 1);
}

void api_on_user_state(struct client *c, MumbleProto__UserState *msg) {
	struct user *u = user_get_by_session(msg->session, &c->env->users);

	bool new = (u == NULL);

	if (new && !msg->name) return;

	pthread_mutex_lock(&c->env->m_user);

	if (new) {
		u = malloc(sizeof(struct user));
		u->session = msg->session;
		u->name = strdup(msg->name);
		u->authenticated = FALSE;
		u->mute = FALSE;
		u->deaf = FALSE;
		u->suppressed = FALSE;
		u->recording = FALSE;
		u->channel = NULL;
		u->address = NULL;
		list_add_tail(&u->l_users, &c->env->users);
	} else if (msg->name) {
		free(u->name);
		u->name = strdup(msg->name);
	}

	if (msg->has_user_id) {
		u->id = msg->user_id;
		u->authenticated = TRUE;
	}

	if (msg->has_mute) u->mute = msg->mute;
	if (msg->has_self_mute) u->mute = msg->self_mute;

	if (msg->has_deaf) u->deaf = msg->deaf;
	if (msg->has_self_deaf) u->deaf = msg->self_deaf;

	if (msg->has_suppress) u->suppressed = msg->suppress;

	if (msg->has_recording) u->recording = msg->recording;

	if (msg->has_channel_id) {
		u->channel = channel_get_by_id(msg->channel_id, &c->env->channels);
		if (!u->channel) {
			u->channel = channel_get_by_id(0, &c->env->channels);
		}
	} else if (new) {
		u->channel = channel_get_by_id(0, &c->env->channels);
	}

	pthread_mutex_unlock(&c->env->m_user);

	if (new && u->session != c->session) {
		task_new_arg(a, l);
		task_push_arg(a, l, struct user *, u);

		plugin_queue_task_all(&c->plugins, user_joined_server_event, a, l);
	}
}

void api_on_user_remove(struct client *c, MumbleProto__UserRemove *msg) {
	struct user *u;

	if (!(u = user_get_by_session(msg->session, &c->env->users))) return;

	pthread_mutex_lock(&c->env->m_user);

	user_free(u);

	pthread_mutex_unlock(&c->env->m_user);
}

void api_on_user_stats(struct client *c, MumbleProto__UserStats *msg) {
	struct user *u;

	if (!(u = user_get_by_session(msg->session,&c->env->users))) return;

	pthread_mutex_lock(&c->env->m_user);

	if (msg->has_address) {
		struct in_addr addr;
		addr.s_addr = *(in_addr_t *)&msg->address.data[msg->address.len - 4];
		if (u->address) free(u->address);
		u->address = strdup(inet_ntoa(addr));
	}

	task_new_arg(a, l);
	task_push_arg(a, l, struct user *, u);

	plugin_queue_task_all(&c->plugins, user_stats_event, a, l);

	pthread_mutex_unlock(&c->env->m_user);
}
