#ifndef AUDIO_H_
#define AUDIO_H_

#include <celt/celt.h>

#include "../celtcodec.h"
#include "connection.h"
#include "message.h"
#include "../types.h"

#define m_AUDIO m_UDPTUNNEL

enum {
	UDP_TYPE_CELT_ALPHA = 0,
	UDP_TYPE_PING = 1,
	UDP_TYPE_SPEEX = 2,
	UDP_TYPE_CELT_BETA = 3
};

enum {
	UDP_TARGET_NORMAL = 0,
	UDP_TARGET_WHISPER_CHANNEL = 1,
	UDP_TARGET_WHISPER_INCOMING = 2,
	UDP_TARGET_SERVER_LOOPBACK = 31
};

typedef int16_t audio_frame[FRAME_SIZE];

struct audio {
	unsigned int term : 1;
	unsigned int len  : 7;
	unsigned char frame[127];
};

struct packet {
	unsigned int type   : 3;
	unsigned int target : 5;
	union {
		struct {
			uint64_t session;
			uint64_t sequence;
			struct audio *audio;
			bool has_positional_audio;
			float x;
			float y;
			float z;
		};
		uint64_t timestamp;
	} payload;
};

#define audio_new(ty, ta) { .type = ty, .target = ta }

void audio_free(struct packet *);

int audio_serialize(struct packet *, unsigned char **, int);

int audio_deserialize(struct packet **, unsigned char *, int);

int audio_send(struct connection *, struct packet *);

int audio_recv(struct connection *, struct packet **);

int audio_celt_encode(struct connection *, struct celtcodec *, CELTEncoder *, struct packet *, audio_frame *, int, bool);

int audio_celt_decode(struct connection *, struct celtcodec *, CELTDecoder *, struct packet *, audio_frame **);

#endif /* AUDIO_H_ */
