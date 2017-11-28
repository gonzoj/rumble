#ifndef SOUND_H_
#define SOUND_H_

#include <celt/celt.h>
#include <libavformat/avformat.h>
#include <lua.h>
#include <pthread.h>
#include <stdlib.h>

#include "../net/audio.h"
#include "../celtcodec.h"
#include "channel.h"
#include "../plugin.h"
#include "../list.h"
#include "../timer.h"
#include "../types.h"

/* forward declaration to avoid circular dependency with client.h */
struct client;

struct environment;

enum {
	SOUND_INPUT_TYPE_FILE,
	SOUND_INPUT_TYPE_BUFFER
};

struct av_input {
	const char *name;
	int type;
	AVFormatContext *fctx;
	void *data;
};

struct av_input_buffer {
	unsigned char *buf;
	int64_t len;
	int64_t off;
};

struct track {
	struct celtcodec *cc;
	CELTDecoder *cdecoder;
	uint64_t session;
	uint64_t sequence;
	int index;
	struct list_head l_tracks;
};

struct streambuf {
	int size;
	audio_frame *frame;
	uint64_t timestamp;
	int index;
	struct list_head tracks;
	pthread_mutex_t m_track;
};

struct stream {
	struct celtcodec *cc;
	CELTEncoder *cencoder;
	bool enabled;
	struct channel *to;
	int delay;
	float volume;
	struct streambuf buffer;
	pthread_mutex_t m_buffer;
	pthread_t tid;
};

struct input {
	int type;
	union {
		char *file;
		struct {
			char *name;
			unsigned char *data;
			int len;
		} buffer;
	};
	float from;
	float to;
	struct plugin *plugin;
	struct list_head l_input;
};

struct playback {
	struct celtcodec *cc;
	CELTEncoder *cencoder;
	bool enabled;
	struct list_head input;
	struct input *current;
	float volume;
	bool next;
	pthread_t tid;
	pthread_cond_t notify;
};

struct sound {
	struct playback playback;
	pthread_mutex_t m_playback;
	struct stream stream;
	pthread_mutex_t m_stream;
};

void sound_av_init();

void sound_start_playback(struct environment *);

void sound_start_playback_from_file(struct client *, const char *, float, float, float);

int lua_sound_start_playback_from_file(lua_State *);

void sound_start_playback_from_buffer(struct client *, const char *, unsigned char *, int, float, float, float);

int lua_sound_start_playback_from_buffer(lua_State *);

void sound_stop_playback(struct client*);

int lua_sound_stop_playback(lua_State *);

void sound_clear_playback(struct client *);

int lua_sound_clear_playback(lua_State *);

void sound_playback_volume_up(struct client *);

int lua_sound_playback_volume_up(lua_State *);

void sound_playback_volume_down(struct client *);

int lua_sound_playback_volume_down(lua_State *);

void sound_create_stream(struct client *, struct channel *, int);

int lua_sound_create_stream(lua_State *);

void sound_destroy_stream(struct client *);

int lua_sound_destroy_stream(lua_State *);

void sound_stream_volume_up(struct client *);

int lua_sound_stream_volume_up(lua_State *);

void sound_stream_volume_down(struct client *);

int lua_sound_stream_volume_down(lua_State *);

void sound_install(struct plugin *);

void api_on_audio_message(struct client *c, struct packet *pkt);

#endif /* SOUND_H_ */
