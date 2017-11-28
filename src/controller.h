#ifndef CONTROLLER_H_
#define CONTROLLER_H_

#include <lua.h>

#include "list.h"
#include "types.h"
#include "api/user.h"

/* forward declaration to avoid circular dependency with client.h */
struct client;

enum {
	PRIV_LEVEL_NORMAL,
	PRIV_LEVEL_AUTH,
	PRIV_LEVEL_ADMIN,
} CONTROLLER_PRIVILEGE_LEVEL;

struct privilege {
	char *user;
	int level;
	struct list_head l_privileges;
};

struct command {
	char *name;
	int priv;
	void (*process)(struct client *, struct user *, int, char **);
};

#define COMMAND_NEW(n, p, f) (struct command) { .name = n, .priv = p, .process = (void (*)(struct client *, struct user *, int, char **)) f }

void controller_clear_privileges(struct list_head *);

void controller_create_privileges(lua_State *);

void controller_load_privileges(const char *, struct list_head *);

void controller_save_privileges(const char *, struct list_head *);

int controller_get_privilege(struct list_head *, struct user *);

bool controller_check_privilege(struct list_head *, struct user *, int);

struct command * controller_get_command(char *);

void controller_process_command(struct client *, struct user *, char *);

#endif /* CONTROLLER_H_ */
