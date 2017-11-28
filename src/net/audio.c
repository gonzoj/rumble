#include <celt/celt.h>
#include <stdlib.h>

#include "audio.h"
#include "../celtcodec.h"
#include "../console.h"
#include "message.h"
#include "../types.h"
#include "varint.h"

#define _CLASS "audio"

#define min(x, y) ((x) < (y) ? (x) : (y))

#define VOICE_HEADER_SIZE 1

#define VOICE_DATA_HEADER_SIZE 1

#define VOICE_POSITIONAL_AUDIO_SIZE (3 * sizeof(float))

#define packet_set_header(p, b, i) (b)[i] = (p->type << 5) | p->target
#define packet_get_header(p, b, i) p->type = b[i] >> 5; p->target = b[i] & 0x1F

#define packet_set_data_header(d, b, i) (b)[i] = (d.term << 7) | d.len
#define packet_get_data_header(d, b, i) d.term = b[i] >> 7; d.len = b[i] & 0x7F

void audio_free(struct packet *p) {
	if (p->type != UDP_TYPE_PING && p->payload.audio) free(p->payload.audio);

	free(p);
}

int audio_serialize(struct packet *p, unsigned char **buf, int off) {
	if (!off) *buf = NULL;

	int i = off;

	*buf = realloc(*buf, i + VOICE_HEADER_SIZE);
	packet_set_header(p, *buf, i);
	i += VOICE_HEADER_SIZE;

	unsigned char *varint;
	int len;
	
	if (p->type == UDP_TYPE_PING) {
		len = varint_encode(p->payload.timestamp, &varint);

		*buf = realloc(*buf, i + len);
		memcpy(*buf + i, varint, len);
		i += len;

		free(varint);
	} else {
		len = varint_encode(p->payload.sequence, &varint);

		*buf = realloc(*buf, i + len);
		memcpy(*buf + i, varint, len);
		i += len;
		free(varint);

		int j;
		bool last;
		for (j = 0, last = FALSE; !last; last = !p->payload.audio[j].term, j++) {
			*buf = realloc(*buf, i + VOICE_DATA_HEADER_SIZE + p->payload.audio[j].len);
			packet_set_data_header(p->payload.audio[j], *buf, i);
			i += VOICE_DATA_HEADER_SIZE;
			if (p->payload.audio[j].len) memcpy(*buf + i, p->payload.audio[j].frame, p->payload.audio[j].len);
			i += p->payload.audio[j].len;
		}

		if (p->payload.has_positional_audio) {
			*buf = realloc(*buf, i + VOICE_POSITIONAL_AUDIO_SIZE);
			memcpy(*buf + i, &p->payload.x, VOICE_POSITIONAL_AUDIO_SIZE);
			i += VOICE_POSITIONAL_AUDIO_SIZE;
		}
	}

	return i - off;
}

int audio_deserialize(struct packet **packet, unsigned char *buf, int len) {
	*packet = malloc(sizeof(struct packet));
	struct packet *p = *packet;

	int i = 0;

	packet_get_header(p, buf, i);
	i += VOICE_HEADER_SIZE;

	if (p->type == UDP_TYPE_PING) {
		i += varint_decode(buf + i, &p->payload.timestamp);
	} else {
		i += varint_decode(buf + i, &p->payload.session);
		i += varint_decode(buf + i, &p->payload.sequence);

		p->payload.audio = NULL;

		int j;
		bool last;
		for (j = 0, last = FALSE; !last; last = !p->payload.audio[j].term, j++) {
			p->payload.audio = realloc(p->payload.audio, (j + 1) * sizeof(struct audio));
			packet_get_data_header(p->payload.audio[j], buf, i);
			i += VOICE_DATA_HEADER_SIZE;
			if (p->payload.audio[j].len) memcpy(p->payload.audio[j].frame, buf + i, p->payload.audio[j].len);
			i += p->payload.audio[j].len;
		}

		if (len > i) {
			p->payload.has_positional_audio = TRUE;
			memcpy(&p->payload.x, buf + i, VOICE_POSITIONAL_AUDIO_SIZE);
			i += VOICE_POSITIONAL_AUDIO_SIZE;
		}
	}

	return i;
}

int audio_send(struct connection *con, struct packet *p) {
	if ((p->type == UDP_TYPE_PING) || con->channel.voice.enabled) {
		unsigned char *buf;
		int len = audio_serialize(p, &buf, 0);

		int s = voice_send(con, buf, len);

		if (buf) free(buf);

		return s;
	} else {
		return message_send(con, m_UDPTUNNEL, p);
	}
}

int audio_recv(struct connection *con, struct packet **p) {
	udp_buffer buf;

	int len = voice_recv(con, buf);

	if (len > 0) {
		audio_deserialize(p, buf, len);
	}

	return len;
}

int audio_celt_encode(struct connection *con, struct celtcodec *cc, CELTEncoder *cencoder, struct packet *p, audio_frame *frames, int n, bool terminator) {
	/*if (p->type == UDP_TYPE_PING) {
		console_error(_CLASS, _NONE, "UDP ping packets do not transmit audio\n");

		return -1;
	} else if (p->type == UDP_TYPE_SPEEX) {
		console_error(_CLASS, _NONE, "speex codec is currently unsupported\n");

		return -1;
	}*/

	/*int use = -1;
	struct celtcodec *cc = celtcodec_get(connection_get_celt_codec_version(con, &use));
	if (!cc) {
		console_error(_CLASS, _NONE, "failed to retrieve CELT codec\n");

		return -1;
	}

	p->type = (use == CELT_ALPHA) ? UDP_TYPE_CELT_ALPHA : UDP_TYPE_CELT_BETA;*/

	int bitrate = connection_get_bitrate(con);

	cc->encoder_ctl(cencoder, CELT_SET_PREDICTION(0));
	cc->encoder_ctl(cencoder, CELT_SET_BITRATE(bitrate));

	p->payload.audio = malloc(sizeof(struct audio) * (n + (terminator ? 1 : 0)));

	int i;
	for (i = 0; i < n; i++) {
		int len = cc->encode(cc, cencoder, frames[i], p->payload.audio[i].frame, min(bitrate / 800, sizeof(p->payload.audio[i].frame)));
		if (len < 0) {
			free(p->payload.audio);

			return -1;
		}

		p->payload.audio[i].len = len;
		p->payload.audio[i].term = (i == n - 1 ? (terminator ? 1 : 0) : 1);
	}

	if (terminator) {
		p->payload.audio[i].term = 0;
		p->payload.audio[i].len = 0;
	}
	
	return i;
}

int audio_celt_decode(struct connection *con, struct celtcodec *cc, CELTDecoder *cdecoder, struct packet *p, audio_frame **frames) {
	/*if (p->type == UDP_TYPE_PING) {
		console_error(_CLASS, _NONE, "UDP ping packets do not transmit audio\n");

		return -1;
	} else if (p->type == UDP_TYPE_SPEEX) {
		console_error(_CLASS, _NONE, "speex codec is currently unsupported\n");

		return -1;
	}

	int use = (p->type == UDP_TYPE_CELT_ALPHA) ? CELT_ALPHA : CELT_BETA;
	struct celtcodec *cc = celtcodec_get(connection_get_celt_codec_version(con, &use));
	if (!cc) {
		console_error(_CLASS, _NONE, "failed to retrieve CELT codec\n");

		return -1;
	}*/

	*frames = NULL;

	int i;
	bool last;
	for (i = 0, last = FALSE; !last; last = !p->payload.audio[i].term, i++) {
		if (!p->payload.audio[i].len) break; /* terminator frame */

		*frames = realloc(*frames, sizeof(audio_frame) * (i + 1));
		if (cc->decode(cc, cdecoder, p->payload.audio[i].frame, p->payload.audio[i].len, (*frames)[i]) < 0) {
			free(*frames);

			return -1;
		}
	}
	
	return i;
}
