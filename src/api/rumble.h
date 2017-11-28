#ifndef RUMBLE_H_
#define RUMBLE_H_

#include <lua.h>

#include "../plugin.h"

int lua_rumble_console_print(lua_State *L);

void rumble_install(struct plugin *);

#endif /* RUMBLE_H_ */
