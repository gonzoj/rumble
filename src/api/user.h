#ifndef USER_H_
#define USER_H_

#include <lua.h>

#include "channel.h"
//#include "../client.h"
#include "../list.h"
#include "../plugin.h"
#include "../net/protobuf/Mumble.pb-c.h"
#include "../types.h"

struct client;

struct user {
	uint32_t session;
	char *name;
	uint32_t id;
	bool authenticated;
	struct channel *channel;
	bool mute;
	bool deaf;
	bool suppressed;
	bool recording;
	char *address;
	struct list_head l_users;
};

struct user * user_get_by_session(uint32_t, struct list_head *);

struct user * user_get_by_name(char *, struct list_head *);

void user_free(struct user *);

int user_get_privilege(struct client *, struct user *u);

int lua_user_get_privilege(lua_State *);

int lua_user_get_by_name(lua_State *);

int lua_user_get_channel(lua_State *);

int lua_user_get_name(lua_State *);

int lua_user_get_address(lua_State *);

void user_send_text_message(struct client *, struct user *, const char *, ...);

int lua_user_send_text_message(lua_State *);

int lua_user_request_stats(lua_State *);

void user_install(struct plugin *);

void api_on_user_state(struct client *, MumbleProto__UserState *);

void api_on_user_remove(struct client *, MumbleProto__UserRemove *);

void api_on_user_stats(struct client *, MumbleProto__UserStats *);

#endif /* USER_H_ */
