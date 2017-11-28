#ifndef CHANNEL_H_
#define CHANNEL_H_

#include <lua.h>

#include "../list.h"
#include "../plugin.h"
#include "../net/protobuf/Mumble.pb-c.h"
#include "../types.h"

struct client;

struct channel {
	uint32_t id;
	struct channel *parent;
	char *name;
	char *description;
	bool temporary;
	int32_t position;
	//struct list_head links;
	struct list_head l_channels;
};

struct channel * channel_get_by_id(uint32_t, struct list_head *);

bool channel_is_valid(struct channel *, struct list_head *);

void channel_free(struct channel *);

bool channel_exists(struct client *, struct channel *, const char *);

int lua_channel_exists(lua_State *L);

void channel_create(struct client *, struct channel *, const char *, const char *, bool);

int lua_channel_create(lua_State *L);

void channel_remove(struct client *, struct channel *);

int lua_channel_remove(lua_State *L);

void channel_join(struct client *, struct channel *);

int lua_channel_join(lua_State *L);

void channel_send_text_message(struct client *, struct channel *, const char *, ...);

int lua_channel_send_text_message(lua_State *L);

void channel_install(struct plugin *);

void api_on_channel_state(struct client *, MumbleProto__ChannelState *);

void api_on_channel_remove(struct client *, MumbleProto__ChannelRemove *);

#endif /* CHANNEL_H_ */
