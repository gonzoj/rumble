#ifndef CONNECTION_H_
#define CONNECTION_H_

#include <openssl/ssl.h>
#include <pthread.h>
#include <sys/socket.h>

#include "crypt.h"
#include "../list.h"
#include "../timer.h"
#include "../types.h"

#define UDP_BUFFER_SIZE 1024

typedef unsigned char udp_buffer[UDP_BUFFER_SIZE];

struct control_message {
	void *buf;
	int len;
	struct list_head l_messages;
};

struct ping {
	unsigned int n;
	float avg;
	unsigned int s;
	float var;
};

struct connection {
	const char *host;
	const char *port;
	int max_bandwidth;
	struct {
		/*struct {
			int codec_version[2];
			int use;
		} celt;*/
		int bitrate;
		int frames;
	} audio;
	pthread_mutex_t m_audio;
	struct timer timestamp;
	//bool shutdown;
	//bool retry;
	struct {
		struct {
			bool connected;
			pthread_t tid;
			const char *cert;
			SSL_CTX *ctx;
			//BIO *bio;
			SSL *ssl;
			int socket;
			struct list_head queue;
			pthread_mutex_t m_queue;
			int notify[2];
			struct ping ping;
		} control;
		struct {
			bool connected;
			bool enabled;
			bool transmit_position;
			int socket;
			struct {
				struct sockaddr addr;
				socklen_t len;
			} remote;
			struct crypt crypt;
			pthread_mutex_t m_crypt;
			struct ping ping;
		} voice;
	} channel;
};

void openssl_init();

void openssl_cleanup();

struct connection * connection_new(const char *, const char *, const char *, int, int);

void connection_free(struct connection *);

void connection_update_ping(struct connection *, struct ping *, uint64_t);

//int connection_get_bandwidth(struct connection *);

//void connectin_adjust_bandwidth(struct connection *);

void connection_set_max_bandwidth(struct connection *, int);

//int connection_get_celt_codec_version(struct connection *, int *);

//void connection_set_celt_codec_version(struct connection *, int, int, int);

int connection_get_bitrate(struct connection *);

int connection_get_frames(struct connection *);

int control_flush_queue(struct connection *);

void control_clear_queue(struct connection *);

int control_open(struct connection *);

void control_close(struct connection *);

int control_send(struct connection *, void *, int);

int control_flush_queue(struct connection *);

void control_clear_queue(struct connection *);

int control_recv(struct connection *, void *, int);

int voice_open(struct connection *);

void voice_close(struct connection *);

int voice_send(struct connection *, unsigned char *, int);

int voice_recv(struct connection *, unsigned char *);

//int connection_open(struct connection *);

//void connection_shutdown(struct connection *);

#endif /* CONNECTION_H_ */
