#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../client.h"
#include "event.h"
#include "user.h"
#include "../console.h"
#include "../plugin.h"

static bool event_emit(struct plugin *p, char *event, int argc) {
	char *handler = calloc(strlen("on") + strlen(event) + 1, sizeof(char));
	strcat(handler, "on");
	strcat(handler, event);

	lua_getglobal(p->L, handler);
	if (!lua_isfunction(p->L, -1)) {
		lua_pop(p->L, 1 + argc);	
		free(handler);

		return TRUE;
	}

	lua_insert(p->L, -1 - argc);

	if (lua_pcall(p->L, argc, 0, 0) != LUA_OK) {
		console_warning("plugin", p->name, "failed to handle %s event: %s\n", event, lua_tostring(p->L, -1));
		lua_pop(p->L, 1);
		free(handler);

		return FALSE;
	}

	free(handler);
	
	return TRUE;
}

void user_joined_server_event(struct plugin *p, void *a, int l) {
	task_pop_arg(a, l, struct user *, u);

	/*lua_getglobal(p->L, "onUserJoinedServer");
	if (!lua_isfunction(p->L, -1)) {
		lua_pop(p->L, 1);
		return;
	}*/

	lua_pushlightuserdata(p->L, u);

	/*if (lua_pcall(p->L, 1, 0, 0) != LUA_OK) {
		console_warning("plugin", p->name, "failed to handle UserJoinedServer event (%s)\n", lua_tostring(p->L, -1));
		lua_pop(p->L, 1);
	}*/

	event_emit(p, "UserJoinedServer", 1);
}

void command_message_event(struct plugin *p, void *a, int l) {
	task_pop_arg(a, l, char *, cmd);
	task_pop_arg(a, l, struct user *, u);

	lua_pushlightuserdata(p->L, u);
	lua_pushstring(p->L, cmd);

	event_emit(p, "CommandMessage", 2);

	free(cmd);
}

void text_message_event(struct plugin *p, void *a, int l) {
	task_pop_arg(a, l, char *, msg);
	task_pop_arg(a, l, struct user *, u);

	lua_pushlightuserdata(p->L, u);
	lua_pushstring(p->L, msg);

	event_emit(p, "TextMessage", 2);

	// argument a is copied, underlying character array is not; segfaults like a boss
	free(msg);
}

void playback_event(struct plugin *p, void *a, int l) {
	task_pop_arg(a, l, char *, name);

	lua_pushstring(p->L, name);

	event_emit(p, "Playback", 1);

	free(name);
}

void tick_event(struct plugin *p, void *a, int l) {
	event_emit(p, "Tick", 0);
}

#include <unistd.h>
void * tick(void *arg) {
	struct client *c = (struct client *) arg;

	while (!c->env->tick.shutdown) {
		// TODO: replace usleep with msleep (currently defined in api/sound.c)
		usleep((1000 / c->env->tick.freq) * 1000);

		plugin_queue_task_all(&c->plugins, tick_event, NULL, 0);
	}

	pthread_exit(NULL);
}

void user_stats_event(struct plugin *p, void *a, int l) {
	task_pop_arg(a, l, struct user *, u);

	lua_pushlightuserdata(p->L, u);

	event_emit(p, "UserStats", 1);

}
