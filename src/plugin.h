#ifndef PLUGIN_H_
#define PLUGIN_H_

#include <lua.h>
#include <pthread.h>
#include <stdlib.h>

#include "list.h"
#include "types.h"

#ifndef LUA_OK
#define LUA_OK 0
#endif

struct plugin {
	struct list_head l_plugins;
	char *file;
	char *name;
	pthread_t tid;
	lua_State *L;
	struct {
		struct list_head queue;
		pthread_cond_t notify;
	} task;
	pthread_mutex_t m_task;
	bool exit;
};

struct task {
	struct list_head l_tasks;
	void (*execute)(struct plugin *, void *, int);
	void *arg;
	int len;
};

#define task_new_arg(a, l) void *a = NULL; int l = 0
#define task_push_arg(a, l, t, v) a = realloc(a, l + sizeof(t)); *(t*)(a + l) = v; l += sizeof(t)
#define task_pop_arg(a, l, t, v) l -= sizeof(t); t v = *(t*)(a + l); if (l == 0) free(a)

struct plugin * plugin_get(struct list_head *, char *);

struct plugin * plugin_get_by_thread(struct list_head *, pthread_t);

void plugin_queue_task(struct plugin *p, void (*)(struct plugin *, void *, int), void *, int);

void plugin_queue_task_all(struct list_head *, void (*)(struct plugin *, void *, int), void *, int);

struct plugin * plugin_load(struct list_head *, char *, char *, char *);

void plugin_unload(struct plugin *);

int plugin_load_all(struct list_head *, char *, char *);

void plugin_unload_all(struct list_head *);

#endif /* PLUGIN_H_ */
