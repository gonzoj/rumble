#include <pthread.h>
#include <stdlib.h>

#include "../client.h"
#include "../config.h"
#include "../controller.h"
#include "environment.h"
#include "event.h"
#include "../handler.h"
#include "../list.h"
#include "../plugin.h"
#include "rumble.h"
#include "sound.h"
#include "user.h"

struct environment * environment_new(struct client *c) {
	struct environment *env = malloc(sizeof(struct environment));

	env->client = c;
	c->env = env; // threads started here access c->env which won't be set if we wait for a return

	INIT_LIST_HEAD(&env->users);
	pthread_mutex_init(&env->m_user, NULL);

	INIT_LIST_HEAD(&env->channels);
	pthread_mutex_init(&env->m_channel, NULL);

	env->sound.playback.cc = NULL;
	env->sound.playback.cencoder = NULL;
	env->sound.playback.volume = settings.volume;
	pthread_mutex_init(&env->sound.m_playback, NULL);
	INIT_LIST_HEAD(&env->sound.playback.input);
	pthread_cond_init(&env->sound.playback.notify, NULL);
	//env->sound.playback.enabled = TRUE;
	env->sound.playback.current = NULL;
	sound_start_playback(env);

	pthread_mutex_init(&env->sound.m_stream, NULL);
	env->sound.stream.enabled = FALSE;

	env->tick.shutdown = FALSE;
	env->tick.freq = 10;
	pthread_create(&env->tick.tid, NULL, tick, c);

	return env;
}

void environment_free(struct environment *env) {
	struct user *u, *_u;
	list_for_each_entry_safe(u, _u, &env->users, l_users) {
		user_free(u);
	}
	pthread_mutex_destroy(&env->m_user);

	struct channel *c, *_c;
	list_for_each_entry_safe(c, _c, &env->channels, l_channels) {
		channel_free(c);
	}
	pthread_mutex_destroy(&env->m_channel);

	//if (env->sound.playback.enabled) sound_stop_playback(env->client);
	pthread_mutex_lock(&env->sound.m_playback);
	env->sound.playback.enabled = FALSE;
	pthread_cond_signal(&env->sound.playback.notify);
	pthread_mutex_unlock(&env->sound.m_playback);
	pthread_join(env->sound.playback.tid, NULL);
	sound_clear_playback(env->client);
	pthread_cond_destroy(&env->sound.playback.notify);
	pthread_mutex_destroy(&env->sound.m_playback);
	if (env->sound.playback.cencoder) env->sound.playback.cc->encoder_destroy(env->sound.playback.cencoder);


	if (env->sound.stream.enabled) sound_destroy_stream(env->client);
	pthread_mutex_destroy(&env->sound.m_stream);

	env->tick.shutdown = TRUE;
	pthread_join(env->tick.tid, NULL);

	free(env);
}

struct environment * environment_get() {
	pthread_t self = pthread_self();

	struct list_head *l = client_list();

	struct client *c;
	list_for_each_entry(c, l, l_clients) {
		struct plugin *p;

		list_for_each_entry(p, &c->plugins, l_plugins) {
			if (pthread_equal(p->tid, self)) return c->env;
		}
	}

	return NULL;
}

void interface_install(struct plugin *p, struct interface *api) {
	lua_newtable(p->L);

	int n = 0;

	int i;
	for (i = 1; (n > 0) || (api[i].type != API_TYPE_INVALID); i++) {
		if (api[i].type == API_TYPE_PACKAGE) {
			lua_newtable(p->L);
			lua_pushstring(p->L, api[i].name);
			lua_pushvalue(p->L, -2);
			lua_settable(p->L, -4);
			n++;
		} else if (api[i].type == API_TYPE_FUNCTION) {
			lua_pushstring(p->L, api[i].name);
			lua_pushcfunction(p->L, api[i].function);
			lua_settable(p->L, -3);
		} else if (api[i].type == API_TYPE_INVALID) {
			n--;
			lua_pop(p->L, 1);
		}
	}

	lua_setglobal(p->L, api[0].name);
}

void environment_install(struct plugin *p) {
	user_install(p);
	channel_install(p);
	sound_install(p);
	rumble_install(p);
}

static void api_on_text_message(struct client *c, MumbleProto__TextMessage *msg) {
	/* --- API tests --- */
	if (!strcmp(msg->message, "playback")) {
		char *file = "./test.mp4";
		sound_start_playback_from_file(c, file, 26.97, 32.5, 1.0);
		user_send_text_message(c, user_get_by_session(msg->actor, &c->env->users), file);
	} else if (!strcmp(msg->message, "playback up")) {
		sound_playback_volume_up(c);
	} else if (!strcmp(msg->message, "playback down")) {
		sound_playback_volume_down(c);
	} else if (!strcmp(msg->message, "playback stop")) {
		sound_stop_playback(c);
	} else if (!strcmp(msg->message, "stream")) {
		struct channel *cn;
		list_for_each_entry(cn, &c->env->channels, l_channels) {
			if (!strcmp(cn->name, "stream")) break;
		}
		sound_create_stream(c, cn, 180);
	} else if (!strcmp(msg->message, "stream up")) {
		sound_stream_volume_up(c);
	} else if (!strcmp(msg->message, "stream down")) {
		sound_stream_volume_down(c);
	} else if (!strcmp(msg->message, "stream stop")) {
		sound_destroy_stream(c);
	}

	if (msg->message[0] == '.') {
		if (strlen(msg->message) < 3) return;

		struct user *u = user_get_by_session(msg->actor, &c->env->users);
		if (!u) return;

		bool private = FALSE;
		int i;
		message_for_each_repeated(msg, session, i) {
			if (c->session == msg->session[i]) {
				private = TRUE;
				break;
			}
		}
		
		if (!private) return;

		char *m = strdup(msg->message);

		char *_s;
		char *name = strtok_r(m, " ", &_s);
		if (!name) {
			goto exit;
		}
		name++;

		if (controller_get_command(name)) {
			goto exit;
		}

		struct plugin *p = plugin_get(&c->plugins, name);
		if (!p) {
			user_send_text_message(c, u, "could not find plugin '%s'", name);
			goto exit;
		}

		char *cmd = strtok_r(NULL, "", &_s);
		if (!cmd || !strlen(cmd)) {
			user_send_text_message(c, u, "please specify a valid command like this: '.%s command'", name);
			goto exit;
		}

		char *_cmd = strdup(cmd);

		task_new_arg(a, l);
		task_push_arg(a, l, struct user *, u);
		task_push_arg(a, l, char *, _cmd);

		plugin_queue_task(p, command_message_event, a, l);

		exit:
		free(m);
	} else {
		struct user *u = user_get_by_session(msg->actor, &c->env->users);
		if (!u) return;

		bool private = FALSE;
		int i;
		message_for_each_repeated(msg, session, i) {
			if (c->session == msg->session[i]) {
				private = TRUE;
				break;
			}
		}
		
		if (!private) return;

		struct plugin *p;
		list_for_each_entry(p, &c->plugins, l_plugins) {
			char *m = strdup(msg->message);

			task_new_arg(a, l);
			task_push_arg(a, l, struct user *, u);
			task_push_arg(a, l, char *, m);

			plugin_queue_task(p, text_message_event, a, l);
		}

		//plugin_queue_task_all(&c->plugins, text_message_event, a, l);

		//free(m);
	}
}

void handler_profile_api(struct list_head *profiles) {
	struct profile *p = malloc(sizeof(struct profile));

	handler_profile_new(p);

	handler_profile_register(p, m_TEXT_MESSAGE, api_on_text_message);
	handler_profile_register(p, m_USER_STATE, api_on_user_state);
	handler_profile_register(p, m_USER_REMOVE, api_on_user_remove);
	handler_profile_register(p, m_USER_STATS, api_on_user_stats);
	handler_profile_register(p, m_CHANNEL_STATE, api_on_channel_state);
	handler_profile_register(p, m_CHANNEL_REMOVE, api_on_channel_remove);
	handler_profile_register(p, m_AUDIO, api_on_audio_message);

	list_add_tail(&p->l_profiles, profiles);
}
