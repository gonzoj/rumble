#ifndef ENVIRONMENT_H_
#define ENVIRONMENT_H_

#include <lua.h>
#include <stdlib.h>

#include "event.h"
#include "../list.h"
#include "../plugin.h"
#include "sound.h"
#include "../types.h"

/* forward declaration to avoid circular dependency with client.h */
struct client;

struct environment {
	struct client *client;
	struct list_head users;
	pthread_mutex_t m_user;
	struct list_head channels;
	pthread_mutex_t m_channel;
	struct sound sound;
	struct tick tick;
};

enum {
	API_TYPE_PACKAGE,
	API_TYPE_FUNCTION,
	API_TYPE_INVALID
};

struct interface {
	int type;
	char *name;
	int (*function)(lua_State *);
};

#define API_PACKAGE_END (struct interface) { .type = API_TYPE_INVALID }
#define API_PACKAGE(n) (struct interface) { . type = API_TYPE_PACKAGE, .name = n }
#define API_FUNCTION(n, f) (struct interface) { .type = API_TYPE_FUNCTION, .name = n, .function = (int (*)(lua_State *)) f }

struct environment * environment_new(struct client *);

void environment_free(struct environment *);

struct environment * environment_get();

void interface_install(struct plugin *, struct interface *);

void environment_install(struct plugin *);

void handler_profile_api(struct list_head *);

#endif /* ENVIRONMENT_H_ */
