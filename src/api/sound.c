#include <errno.h>
#include <lua.h>
#include <lauxlib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../net/audio.h"
#include "../celtcodec.h"
#include "../client.h"
#include "../config.h"
#include "../console.h"
#include "environment.h"
#include "event.h"
#include "../plugin.h"
#include "sound.h"
#include "../timer.h"
#include "../types.h"

#define _CLASS "sound"

#define AV_PROBE_BUFFER_SIZE 4096

#define min(x, y) ((x) < (y) ? (x) : (y))

static struct interface api[] = {
	API_PACKAGE("Sound"),
	API_PACKAGE("Playback"),
	API_FUNCTION("startFromFile", lua_sound_start_playback_from_file),
	API_FUNCTION("startFromBuffer", lua_sound_start_playback_from_buffer),
	API_FUNCTION("stop", lua_sound_stop_playback),
	API_FUNCTION("clear", lua_sound_clear_playback),
	API_FUNCTION("volumeUp", lua_sound_playback_volume_up),
	API_FUNCTION("volumeDown", lua_sound_playback_volume_down),
	API_PACKAGE_END,
	API_PACKAGE("Stream"),
	API_FUNCTION("create", lua_sound_create_stream),
	API_FUNCTION("destroy", lua_sound_destroy_stream),
	API_FUNCTION("volumeUp", lua_sound_stream_volume_up),
	API_FUNCTION("volumeDown", lua_sound_stream_volume_down),
	API_PACKAGE_END,
	API_PACKAGE_END
};

void sound_av_log_callback(void *avcl, int level, const char *format, va_list args) {
	int type;

	if (level < AV_LOG_ERROR) {
		type = CONSOLE_ERROR;
	} else if (level == AV_LOG_WARNING) {
		type = CONSOLE_WARNING;
	} else if (level == AV_LOG_INFO) {
		type = CONSOLE_MESSAGE;
	} else if (level == AV_LOG_VERBOSE) {
		type = CONSOLE_DEBUG;
	} else {
		return;
	}

	console_print_message_v(type, "ffmpeg", (*(AVClass **)avcl)->class_name, format, args);
}

void sound_av_init() {
	av_log_set_callback(sound_av_log_callback);
	// disables ffmpeg output
	//av_log_set_level(AV_LOG_QUIET);
	av_register_all();
}

static void msleep(int ms) {
	struct timespec req, rem;
	req.tv_sec = ms / 1000;
	req.tv_nsec = ms * 1000000L - req.tv_sec * 1000000000L;

	int s = nanosleep(&req, &rem);

	while (s) {
		if (errno == EINTR) {
			req = rem;
			s = nanosleep(&req, &rem);
		} else {
			break;
		}
	}
}

static int sound_update_celt_encoder(struct client *c, struct celtcodec **cc, CELTEncoder **cencoder) {
	int use = -1;
	struct celtcodec *_cc = celtcodec_get(client_get_celt_codec_version(c, &use));
	if (!_cc) return -1;

	if (!*cc) {
		*cc = _cc;
		*cencoder = _cc->encoder_create(_cc);
		if (!*cencoder) return -1;
	} else if (*cc != _cc) {
		(*cc)->encoder_destroy(*cencoder);
		*cc = _cc;
		*cencoder = _cc->encoder_create(_cc);
		if (!*cencoder) return -1;
	}

	return use;
}

static bool sound_update_celt_decoder(struct client *c, int type, struct celtcodec **cc, CELTDecoder **cdecoder) {
	int use = (type == UDP_TYPE_CELT_ALPHA) ? CELT_ALPHA : CELT_BETA;
	struct celtcodec *_cc = celtcodec_get(client_get_celt_codec_version(c, &use));
	if (!_cc) return FALSE;

	if (!*cc) {
		*cc = _cc;
		*cdecoder = _cc->decoder_create(_cc);
		if (!*cdecoder) return FALSE;
	} else if (*cc != _cc) {
		(*cc)->decoder_destroy(*cdecoder);
		*cc = _cc;
		*cdecoder = _cc->decoder_create(_cc);
		if (!*cdecoder) return FALSE;
	}

	return TRUE;
}

static void sound_adjust_volume(audio_frame *f, int n, float v) {
	if (v == 1.0f) return;

	int i, j;
	for (i = 0; i < n; i++) {
		for (j = 0; j < FRAME_SIZE; j++) {
			float t = (float) f[i][j] * v;
			f[i][j] = (int16_t) t;
		}
	}
}

static int sound_av_input_buffer_read(void *opaque, uint8_t *buf, int buf_size) {
	console_warning(_CLASS, "ffmpeq", "call to sound_av_buffer_read\n");
	
	struct av_input_buffer *ib = (struct av_input_buffer *) opaque;

	if (buf_size > ib->len - ib->off) return -1;

	if (buf_size) memcpy(buf, ib->buf + ib->off, buf_size);

	ib->off += buf_size;

	return buf_size;
}

static int sound_av_input_buffer_write(void *opaque, uint8_t *buf, int buf_size) {
	console_warning(_CLASS, "ffmpeg", "call to sound_av_buffer_write\n");

	struct av_input_buffer *ib = (struct av_input_buffer *) opaque;

	if (buf_size > ib->len - ib->off) return -1;

	if (buf_size) memcpy(ib->buf + ib->off, buf, buf_size);

	ib->off += buf_size;

	return buf_size;
}

static int64_t sound_av_input_buffer_seek(void *opaque, int64_t offset, int whence) {
	console_warning(_CLASS, "ffmpeg", "call to sound_av_buffer_seek: %s\n", whence == SEEK_SET ? "SEEK_SET" : whence == SEEK_END ? "SEEK_END" : whence == SEEK_CUR ? "SEEK_CUR" : whence == AVSEEK_SIZE ? "AVSEEK_SIZE" : "INVALID");

	struct av_input_buffer *ib = (struct av_input_buffer *) opaque;

	int64_t off = -1;

	switch (whence) {
		case SEEK_SET: {
			if (offset >= 0 && offset <= ib->len) {
				ib->off = offset;
				off = ib->off;
			}
		}
		break;

		case SEEK_END: {
			if (offset <= 0 && ib->len - offset >= 0) {
				ib->off = ib->len - offset;
				off = ib->off;
			}
		}
		break;

		case SEEK_CUR: {
			if (ib->off + offset >= 0 && ib->off + offset <= ib->len) {
				ib->off += offset;
				off = ib->off;
			}
		}
		break;

		case AVSEEK_SIZE:
			return ib->len;

		default:
			return -1;
	}

	return off;
}

static struct av_input_buffer * sound_av_open_input_buffer(AVFormatContext **fctx, const char *name, unsigned char *buf, int len) {
	struct av_input_buffer *ib = malloc(sizeof(struct av_input_buffer));

	ib->buf = buf;
	ib->len = len;
	ib->off = 0;

	unsigned char *av_buffer = av_malloc(len + FF_INPUT_BUFFER_PADDING_SIZE);

	/* it might be sufficient to implement only seek for AVSEEK_SIZE */
	ByteIOContext *pb = av_alloc_put_byte(av_buffer, len, 1, ib, sound_av_input_buffer_read, sound_av_input_buffer_write, sound_av_input_buffer_seek);
	if (!pb) {
		av_free(av_buffer);

		free(ib->buf);
		free(ib);
		
		return NULL;
	}
	/* shit doesn't work if we don't pre-load the comoplete buffer */
	memcpy(pb->buffer, buf, len);
	memset(pb->buffer + len, 0, FF_INPUT_BUFFER_PADDING_SIZE);

	AVProbeData pd;
	pd.filename = name;
	pd.buf_size = AV_PROBE_BUFFER_SIZE;
	pd.buf = av_malloc(AV_PROBE_BUFFER_SIZE + AVPROBE_PADDING_SIZE);
	memcpy(pd.buf, buf, AV_PROBE_BUFFER_SIZE);
	memset(pd.buf + AV_PROBE_BUFFER_SIZE, 0, AVPROBE_PADDING_SIZE);

	AVInputFormat *ifmt = av_probe_input_format(&pd, 1);

	av_free(pd.buf);

	if (!ifmt) {
		console_error(_CLASS, "ffmpeg", "failed to detect input format for %s\n", name);
		
		av_free(pb);
		av_free(av_buffer);

		free(ib->buf);
		free(ib);
		
		return NULL;
	}

	if (av_open_input_stream(fctx, pb, name, ifmt, NULL)) {
		av_free(pb);
		av_free(av_buffer);

		free(ib->buf);
		free(ib);

		return NULL;
	}

	return ib;
}

static void sound_av_close_input_buffer(struct av_input_buffer *ib, AVFormatContext *fctx) {
	ByteIOContext *pb = fctx->pb;

	av_close_input_stream(fctx);

	av_free(pb->buffer);
	av_free(pb);

	free(ib->buf);
	free(ib);
}

static int sound_av_input_open(struct av_input *in, const char *name, int type, va_list args) {
	if (type == SOUND_INPUT_TYPE_FILE) {
		if (av_open_input_file(&in->fctx, name, NULL, 0, NULL)) {
			console_error(_CLASS, "ffmpeg", "failed to open file %s\n", name);

			return -1;
		}
	} else if (type == SOUND_INPUT_TYPE_BUFFER) {
		unsigned char *buf = va_arg(args, unsigned char *);
		int len = va_arg(args, int);

		if (!(in->data = sound_av_open_input_buffer(&in->fctx, name, buf, len))) {
			console_error(_CLASS, "ffmpeg", "failed to open buffer %s\n", name);

			return -1;
		}
	} else {
		console_error(_CLASS, "ffmpeg", "%s: unsupported input type\n", name);

		return -1;
	}

	in->name = name;
	in->type = type;

	return 0;
}

static void sound_av_input_close(struct av_input *in) {
	if (in->type == SOUND_INPUT_TYPE_FILE) {
		av_close_input_file(in->fctx);
	} else if (in->type == SOUND_INPUT_TYPE_BUFFER) {
		sound_av_close_input_buffer(in->data, in->fctx);
	}
}

static int sound_extract_raw_audio(const char *input, int type, audio_frame **frames, ...) {
	AVFormatContext *fctx = NULL;
	struct av_input in;

	va_list args;
	va_start(args, frames);

	if (sound_av_input_open(&in, input, type, args)) {
		va_end(args);

		return -1;
	}

	va_end(args);

	fctx = in.fctx;

	if (av_find_stream_info(fctx) < 0) {
		console_error(_CLASS, "ffmpeg", "could not locate stream info for input %s\n", input);

		sound_av_input_close(&in);

		return -1;
	}

	AVStream *audio = NULL;
	
	int i;
	for (i = 0; i < fctx->nb_streams; i++) {
		// libavcodec 52.20.0 fix
		if (fctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
		//if (fctx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO) {
			audio = fctx->streams[i];
			break;
		}
	}

	if (!audio) {
		console_error(_CLASS, "ffmpeg", "could not locate audio stream in input %s\n", input);

		sound_av_input_close(&in);

		return -1;
	}

	AVCodecContext *cctx = audio->codec;
	AVCodec *c = avcodec_find_decoder(cctx->codec_id);
	if (!c) {
		console_error(_CLASS, "ffmpeg", "input %s requires unavailable decoder for codec %s\n", input, cctx->codec_name);

		sound_av_input_close(&in);

		return -1;
	}

	if (avcodec_open(cctx, c) < 0) {
		console_error(_CLASS, "ffmpeg", "failed to initialize codec %s for audio stream in input %s\n", cctx->codec_name, input);

		sound_av_input_close(&in);

		return -1;
	}

	ReSampleContext *sctx = av_audio_resample_init(CHANNELS, cctx->channels, SAMPLE_RATE, cctx->sample_rate, SAMPLE_FMT_S16, cctx->sample_fmt, 0, 0, 0, 0);
	if (!sctx) {
		console_error(_CLASS, "ffmpeg", "failed to create resample context for audio stream in input %s\n", input);

		avcodec_close(cctx);

		sound_av_input_close(&in);

		return -1;
	}

	AVPacket packet;
	av_init_packet(&packet);

	int _n = 0;
	int16_t *_frames = NULL;

	while (av_read_frame(fctx, &packet) == 0) {
		if (packet.stream_index == audio->index) {
			uint8_t *_data = packet.data;
			int _size = packet.size;

			uint8_t *data = _data;
			int size = _size;

			int j = 0;

			while (size > 0) {	
				packet.data = data;
				packet.size = size;

				int16_t *frame = av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
				int n = AVCODEC_MAX_AUDIO_FRAME_SIZE;

				int d;
				// libav* 52.20.0 fix
				if ((d = avcodec_decode_audio3(cctx, frame, &n, &packet)) > 0 && n) {
				//if ((d = avcodec_decode_audio2(cctx, frame, &n, packet.data, packet.size)) > 0 && n) {
					int16_t buf[AVCODEC_MAX_AUDIO_FRAME_SIZE / sizeof(int16_t)];

					int s;
					if ((s = audio_resample(sctx, buf, frame, n / (sizeof(int16_t) * cctx->channels))) > 0) {
						_frames = realloc(_frames, (_n + s) * sizeof(int16_t));
						memcpy(_frames + _n, buf, s * sizeof(int16_t));
						_n += s;
					}
				}

				av_free(frame);

				if (d <= 0) break;

				data += d;
				size -= d; 

				j++; 
			}

			if (j > 1) console_debug(_CLASS, "ffmpeg", "decoded %i frame from packet\n", j);

			packet.data = _data;
			packet.size = _size;
		}

		av_free_packet(&packet);
	}

	audio_resample_close(sctx);

	avcodec_close(cctx);

	sound_av_input_close(&in);

	int r = ceil((double) _n / FRAME_SIZE);

	if (r) {
		*frames = calloc(r, sizeof(audio_frame));

		memcpy(*frames, _frames, _n * sizeof(int16_t));
	}

	if (_frames) free(_frames);

	return r;
}

static int sound_extract_target_frames(audio_frame **frames, int n, float from, float to) {
	int len = (int) ((to - from) * FRAMES_PER_SECOND);

	if (len <= 0 || (int) (from * FRAMES_PER_SECOND) > n || (int) (to * FRAMES_PER_SECOND) > n) return -1;

	audio_frame *_frames = *frames;

	*frames = malloc(len * sizeof(audio_frame));
	memcpy(*frames, _frames + (int) (from * FRAMES_PER_SECOND), len * sizeof(audio_frame));
	
	free(_frames);

	return len;
}

static void * playback(void *arg) {
	struct client *c = (struct client *) arg;

	pthread_mutex_lock(&c->env->sound.m_playback);

	idle:
	if (list_empty(&c->env->sound.playback.input)) {
		pthread_cond_wait(&c->env->sound.playback.notify, &c->env->sound.m_playback);

		if (!c->env->sound.playback.enabled) {
			pthread_mutex_unlock(&c->env->sound.m_playback);

			pthread_exit(NULL);
		}
	}

	struct input *input = list_first_entry(&c->env->sound.playback.input, struct input, l_input);

	c->env->sound.playback.current = input;

	pthread_mutex_unlock(&c->env->sound.m_playback);

	if (!input) {
		pthread_mutex_lock(&c->env->sound.m_playback);
		
		goto idle;
	}

	if (input->plugin) {
		task_new_arg(a, l);
		task_push_arg(a, l, char *, strdup(input->type == SOUND_INPUT_TYPE_FILE ? input->file : input->buffer.name));

		plugin_queue_task(input->plugin, playback_event, a, l);
	}

	c->env->sound.playback.next = FALSE;

	int n;
	audio_frame *frames;

	if (input->type == SOUND_INPUT_TYPE_FILE) {
		if ((n = sound_extract_raw_audio(input->file, SOUND_INPUT_TYPE_FILE, &frames)) <= 0) goto exit;
	} else if (input->type == SOUND_INPUT_TYPE_BUFFER) {
		if ((n = sound_extract_raw_audio(input->buffer.name, SOUND_INPUT_TYPE_BUFFER, &frames, input->buffer.data, input->buffer.len)) <= 0) goto exit;
	} else {
		goto exit;
	}

	if (input->from >= 0 && input->to >= 0) {
		if ((n = sound_extract_target_frames(&frames, n, input->from, input->to)) < 0) {
			free(frames);

			goto exit;
		}
	}

	struct timer t = TIMER_INIT;

	uint64_t seq;
	int s;
	int i;
	for (i = 0, seq = 0, s = connection_get_frames(c->con); n && c->env->sound.playback.enabled && !c->env->sound.playback.next; n -= s, i += s, seq += s, s = connection_get_frames(c->con)) {
		if (s > n) s = n;

		int use;
		if ((use = sound_update_celt_encoder(c, &c->env->sound.playback.cc, &c->env->sound.playback.cencoder)) < 0) break;

		struct packet p;

		p.type = (use == CELT_ALPHA) ? UDP_TYPE_CELT_ALPHA : UDP_TYPE_CELT_BETA;

		p.target = UDP_TARGET_NORMAL;

		p.payload.sequence = seq;

		sound_adjust_volume(&frames[i], s, c->env->sound.playback.volume);

		if (audio_celt_encode(c->con, c->env->sound.playback.cc, c->env->sound.playback.cencoder, &p, &frames[i], s, (n - s == 0)) >= 0) {
			p.payload.has_positional_audio = FALSE;

			audio_send(c->con, &p);

			free(p.payload.audio);
		}

		while (!timer_is_elapsed(&t, s * 10 * 1000)) {
			msleep(1);
		}
	}

	free(frames);

	exit:
	//if (c->env->sound.playback.cencoder) c->env->sound.playback.cc->encoder_destroy(c->env->sound.playback.cencoder);

	if (input->type == SOUND_INPUT_TYPE_FILE) {
		free(input->file);
	} else if (input->type == SOUND_INPUT_TYPE_BUFFER) {
		free(input->buffer.name);
	}

	pthread_mutex_lock(&c->env->sound.m_playback);

	list_del(&input->l_input);
	free(input);

	c->env->sound.playback.current = NULL;

	if (c->env->sound.playback.enabled) goto idle;

	pthread_mutex_unlock(&c->env->sound.m_playback);

	pthread_exit(NULL);
}

void sound_start_playback(struct environment *env) {
	env->sound.playback.enabled = TRUE;
	pthread_create(&env->sound.playback.tid, NULL, playback, env->client);
}

void sound_start_playback_from_file(struct client *c, const char *file, float from, float to, float volume) {
	//pthread_mutex_lock(&c->env->sound.m_playback);

	//c->env->sound.playback.cc = NULL;
	//c->env->sound.playback.cencoder = NULL;

	c->env->sound.playback.volume = volume;

	/*c->env->sound.playback.input.type = SOUND_INPUT_TYPE_FILE;
	c->env->sound.playback.input.file = strdup(file);

	c->env->sound.playback.from = from;
	c->env->sound.playback.to = to;*/

	struct input *input = malloc(sizeof(struct input));
	input->type = SOUND_INPUT_TYPE_FILE;
	input->file = strdup(file);
	input->from = from;
	input->to = to;
	input->plugin = plugin_get_by_thread(&c->plugins, pthread_self());

	pthread_mutex_lock(&c->env->sound.m_playback);

	list_add_tail(&input->l_input, &c->env->sound.playback.input);

	pthread_cond_signal(&c->env->sound.playback.notify);

	pthread_mutex_unlock(&c->env->sound.m_playback);

	//c->env->sound.playback.enabled = TRUE;

	//pthread_create(&c->env->sound.playback.tid, NULL, playback, c);
	//pthread_detach(c->env->sound.playback.tid);
}

int lua_sound_start_playback_from_file(lua_State *L) {
	struct environment *env = environment_get();

	const char *file = luaL_checkstring(L, 1);
	if (!file) goto exit;

	float from = -1, to = -1, volume = env->sound.playback.volume;
	if (lua_gettop(L) >= 3) {
		if (lua_isnumber(L, 2) && lua_isnumber(L, 3)) {
			from = (float) lua_tonumber(L, 2);
			to = (float) lua_tonumber(L, 3);
		}
		if (lua_gettop(L) == 4 && lua_isnumber(L, 4)) {
			float v = (float) lua_tonumber(L, 4);
			if (v >= 0.0f) volume = v;
		}
	}

	sound_start_playback_from_file(env->client, file, from, to, volume);

	exit:
	return 0;
}

void sound_start_playback_from_buffer(struct client *c, const char *name, unsigned char *buf, int len, float from, float to, float volume) {
	//pthread_mutex_lock(&c->env->sound.m_playback);

	//c->env->sound.playback.cc = NULL;
	//c->env->sound.playback.cencoder = NULL;

	c->env->sound.playback.volume = volume;

	/*c->env->sound.playback.input.type = SOUND_INPUT_TYPE_BUFFER;
	c->env->sound.playback.input.buffer.name = strdup(name);
	c->env->sound.playback.input.buffer.data = malloc(len);
	memcpy(c->env->sound.playback.input.buffer.data, buf, len);
	c->env->sound.playback.input.buffer.len = len;

	c->env->sound.playback.from = from;
	c->env->sound.playback.to = to;*/

	struct input *input = malloc(sizeof(struct input));
	input->type = SOUND_INPUT_TYPE_BUFFER;
	input->buffer.name = strdup(name);
	input->buffer.data = malloc(len);
	memcpy(input->buffer.data, buf, len);
	input->buffer.len = len;
	input->from = from;
	input->to = to;
	input->plugin = plugin_get_by_thread(&c->plugins, pthread_self());

	pthread_mutex_lock(&c->env->sound.m_playback);

	list_add_tail(&input->l_input, &c->env->sound.playback.input);

	pthread_cond_signal(&c->env->sound.playback.notify);

	pthread_mutex_unlock(&c->env->sound.m_playback);

	//c->env->sound.playback.enabled = TRUE;

	//pthread_create(&c->env->sound.playback.tid, NULL, playback, c);
	//pthread_detach(c->env->sound.playback.tid);
}

int lua_sound_start_playback_from_buffer(lua_State *L) {
	struct environment *env = environment_get();

	const char *name = luaL_checkstring(L, 1);
	if (!name) goto exit;

	unsigned char *buf = (unsigned char *) luaL_checkstring(L, 2);
	if (!buf) goto exit;

	if (!lua_isnumber(L, 3)) goto exit;
	int len = (int) lua_tointeger(L, 3);
	if (len <= 0) goto exit;

	float from = -1, to = -1, volume = env->sound.playback.volume;
	if (lua_gettop(L) >= 5) {
		if (lua_isnumber(L, 4) && lua_isnumber(L, 5)) {
			from = (float) lua_tonumber(L, 4);
			to = (float) lua_tonumber(L, 5);
		}
		if (lua_gettop(L) == 6 && lua_isnumber(L, 6)) {
			float v = (float) lua_tonumber(L, 6);
			if (v >= 0.0f) volume = v;
		}
	}

	sound_start_playback_from_buffer(env->client, name, buf, len, from, to, volume);

	exit:
	return 0;
}

void sound_stop_playback(struct client *c) {
	//c->env->sound.playback.enabled = FALSE;
	c->env->sound.playback.next = TRUE;
}

int lua_sound_stop_playback(lua_State *L) {
	struct environment *env = environment_get();

	sound_stop_playback(env->client);

	return 0;
}

void sound_clear_playback(struct client *c) {
	pthread_mutex_lock(&c->env->sound.m_playback);

	struct input *i, *_i;
	list_for_each_entry_safe(i, _i, &c->env->sound.playback.input, l_input) {
		if (i != c->env->sound.playback.current) {
			list_del(&i->l_input);
			free(i);
		}
	}

	pthread_mutex_unlock(&c->env->sound.m_playback);

	sound_stop_playback(c);
}

int lua_sound_clear_playback(lua_State *L) {
	struct environment *env = environment_get();

	sound_clear_playback(env->client);

	return 0;
}

void sound_playback_volume_up(struct client *c) {
	c->env->sound.playback.volume *= 2.0f;
}

int lua_sound_playback_volume_up(lua_State *L) {
	struct environment *env = environment_get();

	sound_playback_volume_up(env->client);

	return 0;
}

void sound_playback_volume_down(struct client *c) {
	c->env->sound.playback.volume *= 0.5f;
}

int lua_sound_playback_volume_down(lua_State *L) {
	struct environment *env = environment_get();

	sound_playback_volume_down(env->client);

	return 0;
}

static struct track * stream_get_track(struct stream *s, uint64_t session) {
	struct track *t;
	list_for_each_entry(t, &s->buffer.tracks, l_tracks) {
		if (t->session == session) return t;
	}

	return NULL;
}

static struct track * stream_add_track(struct stream *s, uint64_t session, uint64_t sequence) {
	struct track *t = malloc(sizeof(struct track));

	t->cc = NULL;
	t->cdecoder = NULL;

	t->session = session;
	t->sequence = sequence;

	uint64_t timestamp = timer_now() / 1000 + s->delay * 1000;

	pthread_mutex_lock(&s->m_buffer);
	t->index = (((timestamp - s->buffer.timestamp) / (1000 / FRAMES_PER_SECOND)) + s->buffer.index) % s->buffer.size;
	pthread_mutex_unlock(&s->m_buffer);

	list_add(&t->l_tracks, &s->buffer.tracks);

	return t;
}

static void stream_reset_track(struct stream *s, struct track *track) {
	track->sequence = 0;
	uint64_t t = timer_now() / 1000 + s->delay * 1000;
	pthread_mutex_lock(&s->m_buffer);
	track->index = (((t - s->buffer.timestamp) / (1000 / FRAMES_PER_SECOND)) + s->buffer.index) % s->buffer.size;
	pthread_mutex_unlock(&s->m_buffer);
}

static void sound_mix_frame(audio_frame a, audio_frame b, audio_frame c) {
	int i;
	for (i = 0; i < FRAME_SIZE; i++) {
		int s = ((int) a[i] + (int) b[i]) / (a[i] ? 2 : 1);
		if (s > 32767) {
			c[i] = 32767;
		} else if (s < -32768) {
			c[i] = -32768;
		} else {
			c[i] = (int16_t) s;
		}
	}
}

static void stream_add_frame(struct stream *s, struct track *track, audio_frame *f, int n, uint64_t sequence) {
	pthread_mutex_lock(&s->m_buffer);

	if (!s->enabled) goto exit;

	uint64_t t = timer_now() / 1000 + s->delay * 1000;

	if (t + n * (1000 / FRAMES_PER_SECOND) - s->buffer.timestamp >= s->buffer.size * (1000 / FRAMES_PER_SECOND)) {
		s->buffer.size += FRAMES_PER_SECOND;
		s->buffer.frame = realloc(s->buffer.frame, s->buffer.size * sizeof(audio_frame));
		if (s->buffer.index) {
			int n = min(s->buffer.index, FRAMES_PER_SECOND);
			memcpy(&s->buffer.frame[s->buffer.size - FRAMES_PER_SECOND], s->buffer.frame, n * sizeof(audio_frame));
			if (s->buffer.index > FRAMES_PER_SECOND) {
				memcpy(s->buffer.frame, &s->buffer.frame[FRAMES_PER_SECOND], (s->buffer.index - FRAMES_PER_SECOND) * sizeof(audio_frame));
				memset(&s->buffer.frame[s->buffer.index - FRAMES_PER_SECOND], 0, FRAMES_PER_SECOND * sizeof(audio_frame));
			} else if (s->buffer.index < FRAMES_PER_SECOND) {
				memset(&s->buffer.frame[s->buffer.size - FRAMES_PER_SECOND + s->buffer.index], 0, (FRAMES_PER_SECOND - s->buffer.index) * sizeof(audio_frame));
			}
		} else {
			memset(&s->buffer.frame[s->buffer.size - FRAMES_PER_SECOND], 0, FRAMES_PER_SECOND * sizeof(audio_frame));
		}
	}

	int i;
	for (i = 0; i < n; i++) {
		int index = (track->index + i + sequence - track->sequence) % s->buffer.size;
		sound_mix_frame(s->buffer.frame[index], f[i], s->buffer.frame[index]);
	}

	exit:
	pthread_mutex_unlock(&s->m_buffer);
}

static void stream_get_frame(struct stream *s, audio_frame *f, int n) {
	pthread_mutex_lock(&s->m_buffer);

	while (timer_now() / 1000 < s->buffer.timestamp) {
		pthread_mutex_unlock(&s->m_buffer);

		msleep(1);

		pthread_mutex_lock(&s->m_buffer);
	}

	int i;
	for (i = 0; i < n; i++) {
		memcpy(f[i], s->buffer.frame[s->buffer.index], sizeof(audio_frame));
		memset(s->buffer.frame[s->buffer.index], 0, sizeof(audio_frame));

		s->buffer.index = ++s->buffer.index % s->buffer.size;
		s->buffer.timestamp += (1000 / FRAMES_PER_SECOND);
	}

	pthread_mutex_unlock(&s->m_buffer);
}

static void * stream(void *arg) {
	struct client *c = (struct client *) arg;

	uint64_t seq = 0;

	while (c->env->sound.stream.enabled) {
		int s = connection_get_frames(c->con);

		int use;
		if ((use = sound_update_celt_encoder(c, &c->env->sound.stream.cc, &c->env->sound.stream.cencoder)) < 0) break;

		audio_frame frames[s];

		stream_get_frame(&c->env->sound.stream, frames, s);

		struct packet p;

		p.type = (use == CELT_ALPHA) ? UDP_TYPE_CELT_ALPHA : UDP_TYPE_CELT_BETA;

		p.target = UDP_TARGET_WHISPER_CHANNEL;

		p.payload.sequence = seq;

		sound_adjust_volume(frames, s, c->env->sound.stream.volume);

		/* terminator won't be sent reliably depending on thread scheduling */
		if (audio_celt_encode(c->con, c->env->sound.stream.cc, c->env->sound.stream.cencoder, &p, frames, s, !c->env->sound.stream.enabled) >= 0) {
			p.payload.has_positional_audio = FALSE;

			audio_send(c->con, &p);

			free(p.payload.audio);
		}

		seq += s;
	}

	pthread_exit(NULL);
}

void sound_create_stream(struct client *c, struct channel *to, int delay) {
	pthread_mutex_lock(&c->env->sound.m_stream);

	c->env->sound.stream.cc = NULL;
	c->env->sound.stream.cencoder = NULL;

	c->env->sound.stream.to = to;

	c->env->sound.stream.delay = delay;

	c->env->sound.stream.volume = settings.volume;
	
	c->env->sound.stream.buffer.size = delay * FRAMES_PER_SECOND;
	c->env->sound.stream.buffer.frame = delay ? malloc(delay * FRAMES_PER_SECOND * sizeof(audio_frame)) : NULL;
	if (delay) memset(c->env->sound.stream.buffer.frame, 0, delay * FRAMES_PER_SECOND * sizeof(audio_frame));
	c->env->sound.stream.buffer.timestamp = timer_now() / 1000;
	c->env->sound.stream.buffer.index = 0;

	INIT_LIST_HEAD(&c->env->sound.stream.buffer.tracks);

	pthread_mutex_init(&c->env->sound.stream.buffer.m_track, NULL);

	pthread_mutex_init(&c->env->sound.stream.m_buffer, NULL);

	MumbleProto__VoiceTarget__Target t = message_new(VOICE_TARGET__TARGET);
	MumbleProto__VoiceTarget m = message_new(VOICE_TARGET);
	message_set_optional(t, channel_id, to->id);
	message_set_optional(m, id, UDP_TARGET_WHISPER_CHANNEL);
	message_add_repeated(m, targets, &t);
	message_send(c->con, m_VOICE_TARGET, &m);
	message_free_repeated(m, targets);

	c->env->sound.stream.enabled = TRUE;

	pthread_create(&c->env->sound.stream.tid, NULL, stream, c);
}

int lua_sound_create_stream(lua_State *L) {
	struct environment *env = environment_get();

	struct channel *to = lua_touserdata(L, 1);
	if (!channel_is_valid(to, &env->channels)) goto exit;

	int delay = luaL_checkint(L, 2);
	if (delay < 0) goto exit;

	sound_create_stream(env->client, to, delay);

	exit:
	return 0;
}

void sound_destroy_stream(struct client *c) {
	c->env->sound.stream.enabled = FALSE;

	pthread_join(c->env->sound.stream.tid, NULL);

	if (c->env->sound.stream.cencoder) c->env->sound.stream.cc->encoder_destroy(c->env->sound.stream.cencoder);

	pthread_mutex_lock(&c->env->sound.stream.m_buffer);
	pthread_mutex_lock(&c->env->sound.stream.buffer.m_track);

	if (c->env->sound.stream.buffer.frame) free(c->env->sound.stream.buffer.frame);

	struct track *t, *n;
	list_for_each_entry_safe(t, n, &c->env->sound.stream.buffer.tracks, l_tracks) {
		list_del(&t->l_tracks);
		
		if (t->cdecoder) t->cc->decoder_destroy(t->cdecoder);

		free(t);
	}

	pthread_mutex_unlock(&c->env->sound.stream.buffer.m_track);
	pthread_mutex_unlock(&c->env->sound.stream.m_buffer);

	pthread_mutex_destroy(&c->env->sound.stream.buffer.m_track);
	pthread_mutex_destroy(&c->env->sound.stream.m_buffer);

	pthread_mutex_unlock(&c->env->sound.m_stream);
}

int lua_sound_destroy_stream(lua_State *L) {
	struct environment *env = environment_get();

	sound_destroy_stream(env->client);

	return 0;
}

void sound_stream_volume_up(struct client *c) {
	c->env->sound.stream.volume *= 2.0f;
}

int lua_sound_stream_volume_up(lua_State *L) {
	struct environment *env = environment_get();

	sound_stream_volume_up(env->client);

	return 0;
}

void sound_stream_volume_down(struct client *c) {
	c->env->sound.stream.volume *= 0.5f;
}

int lua_sound_stream_volume_down(lua_State *L) {
	struct environment *env = environment_get();

	sound_stream_volume_down(env->client);

	return 0;
}

void sound_install(struct plugin *p) {
	interface_install(p, api);
}

void api_on_audio_message(struct client *c, struct packet *pkt) {
	if (pkt->type == UDP_TYPE_PING) return;

	if (pkt->type == UDP_TYPE_SPEEX) return;

	if (c->env->sound.stream.enabled) {
		audio_frame *frames;

		pthread_mutex_lock(&c->env->sound.stream.buffer.m_track);

		if (!c->env->sound.stream.enabled) {
			pthread_mutex_unlock(&c->env->sound.stream.buffer.m_track);

			return;
		}

		struct track *track = stream_get_track(&c->env->sound.stream, pkt->payload.session);
		if (!track) {
			track = stream_add_track(&c->env->sound.stream, pkt->payload.session, pkt->payload.sequence);
		} else if (pkt->payload.sequence == 0) {
			stream_reset_track(&c->env->sound.stream, track);
		}

		if (!sound_update_celt_decoder(c, pkt->type, &track->cc, &track->cdecoder)) return;

		int s = audio_celt_decode(c->con, track->cc, track->cdecoder, pkt, &frames);

		pthread_mutex_unlock(&c->env->sound.stream.buffer.m_track);

		if (s <= 0) return;

		stream_add_frame(&c->env->sound.stream, track, frames, s, pkt->payload.sequence);

		free(frames);
	}
}
