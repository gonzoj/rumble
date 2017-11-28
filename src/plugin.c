#include <dirent.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "api/environment.h"
#include "list.h"
#include "plugin.h"
#include "types.h"

#define _CLASS "plugin"

static void * plugin(void *arg) {
	struct plugin *self = (struct plugin *) arg;

	struct task *t, *n;

	pthread_mutex_lock(&self->m_task);

	while (!self->exit) {
		if (list_empty(&self->task.queue)) {
			pthread_cond_wait(&self->task.notify, &self->m_task);

			if (self->exit) break;
		}

		t = list_first_entry(&self->task.queue, struct task, l_tasks);

		pthread_mutex_unlock(&self->m_task);

		t->execute(self, t->arg, t->len);

		pthread_mutex_lock(&self->m_task);

		list_del(&t->l_tasks);

		free(t);
	}

	list_for_each_entry_safe(t, n, &self->task.queue, l_tasks) {
		list_del(&t->l_tasks);

		free(t);
	}

	pthread_mutex_unlock(&self->m_task);

	pthread_exit(NULL);
}

struct plugin * plugin_get(struct list_head *plugins, char *name) {
	struct plugin *p;
	list_for_each_entry(p, plugins, l_plugins) {
		if (!strcmp(p->name, name)) return p;
	}

	return NULL;
}

struct plugin * plugin_get_by_thread(struct list_head *plugins, pthread_t tid) {
	struct plugin *p;
	list_for_each_entry(p, plugins, l_plugins) {
		if (pthread_equal(p->tid, tid)) return p;
	}

	return NULL;
}

void plugin_queue_task(struct plugin *p, void (*e)(struct plugin *, void *, int), void *a, int l) {
	// TODO: sooner or later we will segfault here, due to async playback thread queueing a task for a plugin that has been unloaded
	if (p->exit) return;

	struct task *t = malloc(sizeof(struct task));

	t->execute = e;
	t->arg = a;
	t->len = l;

	pthread_mutex_lock(&p->m_task);
	
	list_add_tail(&t->l_tasks, &p->task.queue);

	pthread_cond_signal(&p->task.notify);

	pthread_mutex_unlock(&p->m_task);
}

void plugin_queue_task_all(struct list_head *list, void (*e)(struct plugin *, void *, int), void *a, int l) {
	struct plugin *p;
	list_for_each_entry(p, list, l_plugins) {
		// TODO: first plugin pops arguments, other plugins crash after that
		void *_a = NULL;
		if (a) {
			_a = malloc(l);
			memcpy(_a, a, l);
		}
		plugin_queue_task(p, e, _a, l);
	}
	if (a) {
		free(a);
	}
}

static void plugin_install_package_path(struct plugin *p, char *dir) {
	if (!strlen(dir)) return;

	int len = strlen(";") + strlen(dir) + strlen("?.lua") + 1;
	char *packages = calloc(len, sizeof(char));
	strcat(packages, ";");
	strcat(packages, dir);
	if (packages[strlen(packages) - 1] != '/') {
		packages = realloc(packages, ++len * sizeof(char));
		strcat(packages, "/");
	}
	strcat(packages, "?.lua");

	lua_getglobal(p->L, "package");
	lua_getfield(p->L, -1, "path");
	const char *path = lua_tostring(p->L, -1);
	lua_pop(p->L, 1);

	char *new = calloc(strlen(path) + len, sizeof(char));
	strcat(new, path);
	strcat(new, packages);

	lua_pushstring(p->L, new);
	lua_setfield(p->L, -2, "path");

	lua_pop(p->L, 1);

	free(new);

	free(packages);
}

struct plugin * plugin_load(struct list_head *l, char *dir, char *name, char *packagedir) {
	char *file = NULL;
	int len = 1;
	if (strlen(dir)) {
		file = strdup(dir);
		if (file[strlen(file) - 1] != '/') {
			file = realloc(file, len + strlen(file));
			strcat(file, "/");
		}
		len += strlen(file);
	}
	file = realloc(file, len + strlen(name));
	if (len == 1) {
		*file = '\0';
	}
	strcat(file, name);
	len += strlen(name);
	if (!strstr(file, ".lua") || strstr(file, ".lua")[strlen(".lua")] != '\0') {
		file = realloc(file, len + strlen(".lua"));
		strcat(file, ".lua");
	}

	struct plugin *p;

	list_for_each_entry(p, l, l_plugins) {
		if (!strcmp(p->file, file)) {
			plugin_unload(p);
			break;
		}
	}

	console_message(_CLASS, _NONE, "loading plugin %s...\n", file);

	p = malloc(sizeof(struct plugin));

	p->L = luaL_newstate();

	luaL_openlibs(p->L);

	environment_install(p);

	plugin_install_package_path(p, packagedir);

	if (luaL_dofile(p->L, file) != LUA_OK) {
		console_error(_CLASS, _NONE, "failed to load plugin %s: %s\n", file, lua_tostring(p->L, -1));
		lua_pop(p->L, 1);

		lua_close(p->L);

		free(p);

		free(file);

		return NULL;
	}

	p->file = strdup(file);

	char *c = strdup(file);

	*strstr(c, ".lua") = '\0';

	char *d;
	if ((d = strrchr(c, '/'))) {
		d++;
	} else {
		d = c;
	}

	p->name = strdup(d);

	free(c);

	lua_getglobal(p->L, p->name);

	if (!lua_istable(p->L, -1)) {
		console_error(_CLASS, p->name, "failed to retrieve plugin object\n");
	} else {
		lua_pushstring(p->L, "onLoad");
		lua_gettable(p->L, -2);

		console_message(_CLASS, p->name, "initializing...\n");

		if (lua_pcall(p->L, 0, 0, 0) != LUA_OK) { 
			console_error(_CLASS, p->name, "failed to execute onLoad routine: %s\n", lua_tostring(p->L, -1));
			lua_pop(p->L, 1);
		} else {
			lua_pop(p->L, 1);

			INIT_LIST_HEAD(&p->task.queue);

			pthread_cond_init(&p->task.notify, NULL);
			pthread_mutex_init(&p->m_task, NULL);

			p->exit = FALSE;

			pthread_create(&p->tid, NULL, plugin, (void *) p);

			list_add_tail(&p->l_plugins, l);

			free(file);

			return p;
		}
	}

	lua_pop(p->L, 1);

	lua_close(p->L);

	free(p->name);

	free(p->file);

	free(p);

	free(file);

	return NULL;
}

void plugin_unload(struct plugin *p) {
	console_message(_CLASS, _NONE, "unloading plugin %s...\n", p->name);

	pthread_mutex_lock(&p->m_task);

	p->exit = TRUE;

	pthread_cond_signal(&p->task.notify);

	pthread_mutex_unlock(&p->m_task);

	pthread_join(p->tid, NULL);

	lua_getglobal(p->L, p->name);

	if (!lua_istable(p->L, -1)) {
		console_error(_CLASS, p->name, "failed to retrieve plugin object\n");
	} else {
		lua_pushstring(p->L, "onExit");
		lua_gettable(p->L, -2);

		console_message(_CLASS, p->name, "exiting...\n");

		if (lua_pcall(p->L, 0, 0, 0) != LUA_OK) { 
			console_error(_CLASS, p->name, "failed to execute onExit routine: %s\n", lua_tostring(p->L, -1));
			lua_pop(p->L, 1);
		}
	}

	lua_pop(p->L, 1);

	lua_close(p->L);

	pthread_mutex_destroy(&p->m_task);
	pthread_cond_destroy(&p->task.notify);

	list_del(&p->l_plugins);

	free(p->name);

	free(p->file);

	free(p);
}


int plugin_load_all(struct list_head *l, char *dir, char *packagedir) {
	int i = 0;

	console_message(_CLASS, _NONE, "loading plugins from directory %s\n", dir);

	DIR *plugins = opendir(dir);

	if (!plugins) {
		console_error(_CLASS, _NONE, "failed to open plugin directory %s\n", dir);

		return -1;
	}

	struct dirent *file;
	while ((file = readdir(plugins))) {
		if (!strstr(file->d_name, ".lua") || strstr(file->d_name, ".lua")[strlen(".lua")] != '\0') continue;

		/*char *plugin = strdup(dir);

		if (plugin[strlen(plugin) - 1] != '/') {
			plugin = realloc(plugin, strlen(plugin) + 2);
			strcat(plugin, "/");
		}

		plugin = realloc(plugin, strlen(plugin) + strlen(file->d_name) + 1);
		strcat(plugin, file->d_name);**/

		char *plugin = strdup(file->d_name);

		if (plugin_load(l, dir, plugin, packagedir)) i++;

		free(plugin);
	}

	closedir(plugins);

	if (i) console_message(_CLASS, _NONE, "%i plugin(s) successfully loaded\n", i);

	return i;
}

void plugin_unload_all(struct list_head *l) {
	int i = 0;

	struct plugin *p, *n;
	list_for_each_entry_safe(p, n, l, l_plugins) {
		plugin_unload(p);
		i++;
	}
	
	console_message(_CLASS, _NONE, "%i plugin(s) unloaded\n", i);
}
