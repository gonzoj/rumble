#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "console.h"
#include "controller.h"
#include "list.h"
#include "plugin.h"
#include "types.h"
#include "api/user.h"

#define _CLASS "controller"

static const char * s_priv_level[] = {
	"Privilege.Normal",
	"Privilege.Authenticated",
	"Privilege.Admin"
};

static void controller_command_load(struct client *, struct user *, int, char **);

static struct command commands[] = {
	COMMAND_NEW("load", PRIV_LEVEL_ADMIN, controller_command_load)
};

void controller_clear_privileges(struct list_head *l) {
	struct privilege *p, *_p;
	list_for_each_entry_safe(p, _p, l, l_privileges) {
		list_del(&p->l_privileges);
		// free line 88
		free(p->user);
		free(p);
	}
}

void controller_create_privileges(lua_State *L) {
	lua_newtable(L);

	lua_pushstring(L, "Normal"); 
	lua_pushnumber(L, PRIV_LEVEL_NORMAL);
	lua_settable(L, -3);

	lua_pushstring(L, "Authenticated");
	lua_pushnumber(L, PRIV_LEVEL_AUTH);
	lua_settable(L, -3);

	lua_pushstring(L, "Admin");
	lua_pushnumber(L, PRIV_LEVEL_ADMIN);
	lua_settable(L, -3);
}

void controller_load_privileges(const char *file, struct list_head *l) {
	console_message(_CLASS, _NONE, "loading privileges...\n");

	lua_State *L = luaL_newstate();

	luaL_openlibs(L);

	controller_create_privileges(L);
	lua_setglobal(L, "Privilege");

	if (luaL_dofile(L, file) != LUA_OK) {
		console_warning(_CLASS, _NONE, "failed to load privileges from %s (%s)\n", file, lua_tostring(L, -1));
		lua_pop(L, 1);

		lua_close(L);

		return;
	}

	lua_getglobal(L, "_G");

	int G = lua_gettop(L);	
	for (lua_pushnil(L); lua_next(L, G); lua_pop(L, 1)) {
		if (!lua_isstring(L, -2) || lua_isnumber(L, -2)) continue;
		const char *key = lua_tostring(L, -2);

		if (!lua_isnumber(L, -1)) continue;
		int value = lua_tointeger(L, -1);
		if (value < PRIV_LEVEL_NORMAL || value > PRIV_LEVEL_ADMIN) continue;

		struct privilege *p = malloc(sizeof(struct privilege));

		p->user = strdup(key);
		p->level = value;

		list_add(&p->l_privileges, l);

		console_message(_CLASS, _NONE, "adding %s %s\n", strchr(s_priv_level[p->level], '.') + 1, p->user);
	}

	lua_pop(L, 1);

	lua_close(L);
}

void controller_save_privileges(const char *file, struct list_head *l) {
	if (list_empty(l)) return;

	console_message(_CLASS, _NONE, "saving privileges to %s...\n", file);

	FILE *f = fopen(file, "w");
	if (!f) {
		console_error(_CLASS, _NONE, "failed to open %s\n", file);
		return;
	}

	struct privilege *p;
	list_for_each_entry(p, l, l_privileges) {
		fprintf(f, "%s = %s\n", p->user, s_priv_level[p->level]);

		console_message(_CLASS, _NONE, "adding %s %s\n", strchr(s_priv_level[p->level], '.') + 1, p->user);
	}

	fclose(f);
}

int controller_get_privilege(struct list_head *l, struct user *u) {
	struct privilege *p;
	list_for_each_entry(p, l, l_privileges) {
		if (!strcmp(p->user, u->name)) return p->level;
	}

	return u->authenticated ? PRIV_LEVEL_AUTH : PRIV_LEVEL_NORMAL;
}

bool controller_check_privilege(struct list_head *l, struct user *u, int level) {
	switch (level) {
		case PRIV_LEVEL_NORMAL:
			return TRUE;

		case PRIV_LEVEL_AUTH:
			return u->authenticated;

		case PRIV_LEVEL_ADMIN:
		default: {
			if (!u->authenticated) break;

			struct privilege *p;
			list_for_each_entry(p, l, l_privileges) {
				if (!strcmp(p->user, u->name)) {
					if (p->level >= level) return TRUE;
					else break;
				}
			}
		}
	}

	return FALSE;
}

static void controller_command_load(struct client *c, struct user *u, int argc, char **argv) {
	if (argc < 2) {
		user_send_text_message(c, u, "please specify what to load: %s [plugin|privileges]", argv[0]);
	}

	if (!strcmp(argv[1], "plugin")) {
		if (argc < 3) {
			user_send_text_message(c, u, "please specify the plugin to load: %s plugin 'plugin'", argv[0]);
		} else if (plugin_load(&c->plugins, c->plugindir, argv[2], c->packagedir)) {
			user_send_text_message(c, u, "plugin %s loaded successfully", argv[2]);
		} else {
			user_send_text_message(c, u, "failed to load plugin %s", argv[2]);
		}
	} else if (!strcmp(argv[1], "privileges")) {
		pthread_mutex_lock(&c->m_privilege);

		controller_clear_privileges(&c->privileges);
		controller_load_privileges(c->f_privilege, &c->privileges);

		pthread_mutex_unlock(&c->m_privilege);

		user_send_text_message(c, u, "privileges loaded");
	} else {
		user_send_text_message(c, u, "invalid command '%s %s'", argv[0], argv[1]);
	}
}

struct command * controller_get_command(char *name) {
	int i;
	for (i = 0; i < sizeof(commands) / sizeof(struct command); i++) {
		if (!strcmp(name, commands[i].name)) return commands + i;
	}

	return NULL;
}

void controller_process_command(struct client *c, struct user *u, char *message) {
	char *m = strdup(message);

	char *_s;
	char *name = strtok_r(m, " ", &_s);

	struct command *command = controller_get_command(name);
	if (command) {
		if (controller_check_privilege(&c->privileges, u, command->priv)) {
			char **argv = malloc(sizeof(char *));
			int argc = 0;

			argv[argc++] = name;

			char *arg;
			while ((arg = strtok_r(NULL, " ", &_s))) {
				argv = realloc(argv, argc * sizeof(char *) + sizeof(char *));
				argv[argc++] = arg;
			}

			command->process(c, u, argc, argv);

			free(argv);
		} else {
			user_send_text_message(c, u, "privilege violation: command '%s' requires at least privilege %s", name, strchr(s_priv_level[command->priv], '.') + 1);
		}
	} else if (!plugin_get(&c->plugins, name)) {
		user_send_text_message(c, u, "unknown command '%s'", name);
	}

	free(m);
}
