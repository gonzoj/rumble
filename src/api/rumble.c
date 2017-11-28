#include <lauxlib.h>
#include <lua.h>
#include <pthread.h>

#include "../client.h"
#include "../console.h"
#include "environment.h"
#include "../plugin.h"
#include "rumble.h"

static struct interface api[] = {
	API_PACKAGE("Rumble"),
	API_PACKAGE("Console"),
	API_FUNCTION("print", lua_rumble_console_print),
	API_PACKAGE_END,
	API_PACKAGE_END
};

int lua_rumble_console_print(lua_State *L) {
	struct environment *env = environment_get();

	const char *m = luaL_checkstring(L, 1);
	if (!m) goto exit;

	/* hack for plugins calling print in onLoad or onExit which doesn't run in plugin thread */
	if (env) {
		struct plugin *p = plugin_get_by_thread(&env->client->plugins, pthread_self());
		if (!p) goto exit;

		console_message("plugin", p->name, m);
	} else {
		print_message(m);
	}

	exit:
	return 0;
}

void rumble_install(struct plugin *p) {
	interface_install(p, api);
}
