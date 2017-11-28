#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "connection.h"
#include "../console.h"
#include "crypt.h"
#include "../list.h"
#include "../timer.h"
#include "../types.h"

#define _CLASS "connection"
#define _CONTROL "control"
#define _VOICE "voice"

static char s_err[512];

#define save_error_string(e) strerror_r(e, s_err, 512)
#define get_error_string() s_err

#define max(x, y) (x < y ? y : x)

static pthread_mutex_t *openssl_lock;

static void openssl_lock_callback(int mode, int n, const char *file, int line) {
	(void) file;
	(void) line;

	if (mode & CRYPTO_LOCK) {
		pthread_mutex_lock(openssl_lock + n);
	} else {
		pthread_mutex_unlock(openssl_lock + n);
	}
}

// openssl-0.9.8 fix
//static void openssl_threadid_callback(CRYPTO_THREADID *id) {
//	/* pthread_t should be considered opaque; however, the type happens to be 'unsigned long int' on most platforms */
//	CRYPTO_THREADID_set_numeric(id, (unsigned long) pthread_self());
//}
static unsigned long openssl_threadid_callback(void) {
	return (unsigned long) pthread_self();
}

void openssl_init() {
	SSL_load_error_strings();
	SSL_library_init();

	openssl_lock = malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));

	int i;
	for (i = 0; i < CRYPTO_num_locks(); i++) {
		pthread_mutex_init(openssl_lock + i, NULL);
	}

	CRYPTO_set_locking_callback(openssl_lock_callback);
	// openssl-0.9.8 fix
	//CRYPTO_THREADID_set_callback(openssl_threadid_callback);
	CRYPTO_set_id_callback(openssl_threadid_callback);
}

void openssl_cleanup() {
	ERR_free_strings();

	int i;
	for (i = 0; i < CRYPTO_num_locks(); i++) {
		pthread_mutex_destroy(openssl_lock + i);
	}

	free(openssl_lock);
}

struct connection * connection_new(const char *host, const char *port, const char *cert, int bitrate, int frames) {
	struct connection *con = malloc(sizeof(struct connection));
	memset(con, 0, sizeof(struct connection));

	con->channel.control.connected = con->channel.voice.connected = FALSE;

	con->host = host;
	con->port = port;

	con->max_bandwidth = -1;

	timer_new(&con->timestamp);

	//con->shutdown = FALSE;

	//con->retry = TRUE;

	if (cert && !strlen(cert)) {
		con->channel.control.cert = NULL;
	} else {
		con->channel.control.cert = cert;
	}

	/*con->audio.celt.codec_version[0] = -1;
	con->audio.celt.codec_version[1] = -1;
	con->audio.celt.use = 0;*/
	con->audio.bitrate = bitrate;
	con->audio.frames = frames;

	pthread_mutex_init(&con->channel.control.m_queue, NULL);

	pthread_mutex_init(&con->m_audio, NULL);

	return con;
}

void connection_free(struct connection *con) {
	pthread_mutex_destroy(&con->channel.control.m_queue);

	pthread_mutex_destroy(&con->m_audio);

	free(con);
}

void connection_update_ping(struct connection *con, struct ping *p, uint64_t ts) {
	float ping = (float) (timer_elapsed(&con->timestamp) - ts) / 1000.0;

	float prev_avg = p->avg;

	if (!++p->n) {
		p->n++;
		p->avg = 0;
		p->s = 0;
		p->var = 0;
	}

	p->avg += (ping - prev_avg) / p->n;

	p->s += (ping - prev_avg) * (ping - p->avg);
	p->var = sqrt(p->s / p->n);
}

static int connection_get_bandwidth(struct connection *con) {
	int overhead = 20 + 8 + 4 + 1 + 2 + (con->channel.voice.transmit_position ? 12 : 0) + (con->channel.voice.enabled ? 0 : 12) + con->audio.frames;
	overhead *= (800 / con->audio.frames);

	int bandwidth = overhead + con->audio.bitrate;

	return bandwidth;
}

static void connection_adjust_bandwidth(struct connection *con) {
	if (con->max_bandwidth == -1) return;

	int bitrate = con->audio.bitrate;
	int frames = con->audio.frames;

	if (connection_get_bandwidth(con) > con->max_bandwidth) {
		if (con->audio.frames <= 4 && con->max_bandwidth <= 32000) con->audio.frames = 4;
		else if (con->audio.frames == 1 && con->max_bandwidth <= 64000) con->audio.frames = 2;
		else if (con->audio.frames == 2 && con->max_bandwidth <= 48000) con->audio.frames = 4;

		while (con->audio.bitrate > 8000 && connection_get_bandwidth(con)) {
			con->audio.bitrate -= 1000;
		}
	}

	if (con->audio.bitrate <= 8000) con->audio.bitrate = 8000;

	if (bitrate != con->audio.bitrate || frames != con->audio.frames) {
		console_lock();
		console_message(_CLASS, _NONE, "server bandwidth is only %i kbit/s\n", con->max_bandwidth / 1000);
		console_message(_CLASS, _NONE, "audio quality adjusted to %i kbits/s (%i ms)\n", con->audio.bitrate / 1000, con->audio.frames * 10);
		console_unlock();
	}
}

void connection_set_max_bandwidth(struct connection *con, int bandwidth) {
	if (con->max_bandwidth == bandwidth) return;

	pthread_mutex_lock(&con->m_audio);

	con->max_bandwidth = bandwidth;

	connection_adjust_bandwidth(con);

	pthread_mutex_unlock(&con->m_audio);
}

/*int connection_get_celt_codec_version(struct connection *con, int *use) {
	pthread_mutex_lock(&con->m_audio);

	int version;

	if (*use == -1) {
		version = con->audio.celt.codec_version[con->audio.celt.use];
		*use = con->audio.celt.use;
	} else {
		version = con->audio.celt.codec_version[*use];
	}

	pthread_mutex_unlock(&con->m_audio);

	return version;
}

void connection_set_celt_codec_version(struct connection *con, int alpha, int beta, int use) {
	pthread_mutex_lock(&con->m_audio);

	con->audio.celt.codec_version[0] = alpha;
	con->audio.celt.codec_version[1] = beta;
	con->audio.celt.use = use;

	pthread_mutex_unlock(&con->m_audio);
}*/

int connection_get_bitrate(struct connection *con) {
	pthread_mutex_lock(&con->m_audio);

	int bitrate = con->audio.bitrate;

	pthread_mutex_unlock(&con->m_audio);

	return bitrate;
}

int connection_get_frames(struct connection *con) {
	pthread_mutex_lock(&con->m_audio);

	int frames = con->audio.frames;

	pthread_mutex_unlock(&con->m_audio);

	return frames;
}

static struct addrinfo * connection_resolve_host(const char *host, const char *port, int type, int flags) {
	struct addrinfo hints, *result;
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED | flags;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = type;
	hints.ai_protocol = 0;
	
	int r;
	if ((r = getaddrinfo(host, port, &hints, &result))) {
		console_error(_CLASS, _NONE, "failed to resolve host %s:%s (%s)\n", host, port, gai_strerror(r));

		return NULL;
	}

	return result;
}

static int connection_set_nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, (flags | O_NONBLOCK))) {
		save_error_string(errno);

		return -1;
	}

	return 0;
}

static int connection_init_socket(int *s, struct addrinfo *info, bool block, int (*init)(int, const struct sockaddr *, socklen_t)) {
	struct addrinfo *i;
	for (i = info; i; i = i->ai_next) {
		if ((*s = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) < 0) {
			save_error_string(errno);

			continue;
		}

		if (!block) {
			if (connection_set_nonblock(*s)) {
				close(*s);

				continue;
			}
		}

		int error = 0;
		socklen_t len = sizeof(int);


		if (init(*s, i->ai_addr, i->ai_addrlen)) {
			if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
				struct timeval tv = { tv.tv_sec = 10, tv.tv_usec };

				fd_set wfds;
				FD_ZERO(&wfds);
				FD_SET(*s, &wfds);

				if (select(*s + 1, NULL, &wfds, NULL, &tv) > 0) {
					if (!getsockopt(*s, SOL_SOCKET, SO_ERROR, &error, &len)) {
						if (!error) break;
					}
				}
			}
		} else {
			break;
		}

		if (error) {
			save_error_string(error);
		} else {
			save_error_string(errno);
		}

		close(*s);
	}

	return i == NULL ? -1 : 0;
}

static int connection_compare_hosts(struct sockaddr *a, socklen_t a_len, struct sockaddr *b, socklen_t b_len, int flags) {
	char host_a[512], port_a[512], host_b[512], port_b[512];

	if (getnameinfo(a, a_len, host_a, 512, port_a, 512, flags) || getnameinfo(b, b_len, host_b, 512, port_b, 512, flags)) {
		return -1;
	} else {
		return strcmp(host_a, host_b) || strcmp(port_a, port_b);
	}
}

int control_flush_queue(struct connection *con) {
	if (!pthread_equal(pthread_self(), con->channel.control.tid)) return -1;

	int t = 0;

	pthread_mutex_lock(&con->channel.control.m_queue);

	struct control_message *m, *n;
	list_for_each_entry_safe(m, n, &con->channel.control.queue, l_messages) {
		if (control_send(con, m->buf, m->len) < 0) {
			t = -1;
			
			break;
		}

		t += m->len;

		list_del(&m->l_messages);
	
		free(m->buf);
		free(m);
	}

	while (TRUE) {
		char b;
		if (read(con->channel.control.notify[0], &b, 1) <= 0) break;
	}

	pthread_mutex_unlock(&con->channel.control.m_queue);

	return t;
}

void control_clear_queue(struct connection *con) {
	pthread_mutex_lock(&con->channel.control.m_queue);

	struct control_message *m, *n;
	list_for_each_entry_safe(m, n, &con->channel.control.queue, l_messages) {
		list_del(&m->l_messages);

		free(m->buf);
		free(m);
	}

	while (TRUE) {
		char b;
		if (read(con->channel.control.notify[0], &b, 1) <= 0) break;
	}

	pthread_mutex_unlock(&con->channel.control.m_queue);
}

int control_open(struct connection *con) {
	const char *host = con->host;
	const char *port = con->port;
	const char *cert = con->channel.control.cert;

	//char buf[512];

	if (!(con->channel.control.ctx = SSL_CTX_new(TLSv1_client_method()))) {
		console_error(_CLASS, _CONTROL, "failed to create SSL context object\n");

		goto print_err;
	}

	if (cert) {
		if (SSL_CTX_use_certificate_file(con->channel.control.ctx, cert, SSL_FILETYPE_PEM) != 1) {
			console_error(_CLASS, _CONTROL, "failed to load certificate from file %s\n", cert);

			goto free_ctx;
		}
		if (SSL_CTX_use_PrivateKey_file(con->channel.control.ctx, cert, SSL_FILETYPE_PEM) != 1) {
			console_error(_CLASS, _CONTROL, "failed to load private key fromf ile %s\n", cert);

			goto free_ctx;
		}
	}

	if (!(con->channel.control.ssl = SSL_new(con->channel.control.ctx))) {
		console_error(_CLASS, _CONTROL, "failed to create SSL object\n");

		goto free_ctx;
	}

	//SSL_set_mode(con->channel.control.ssl, SSL_MODE_AUTO_RETRY);

	struct addrinfo *result = connection_resolve_host(host, port, SOCK_STREAM, 0);
	if (!result) {
		goto free_ssl;
	}

	if (connection_init_socket(&con->channel.control.socket, result, FALSE, connect)) {
		freeaddrinfo(result);

		console_error(_CLASS, _CONTROL, "failed to connect a socket to %s:%s (%s)\n", host, port, get_error_string());

		goto free_ssl;
	}

	freeaddrinfo(result);

	/*int flags = fcntl(con->channel.control.socket, F_GETFL);
	if (flags < 0 || fcntl(con->channel.control.socket, F_SETFL, (flags | O_NONBLOCK))) {
		printf("connection: error: failed to set socket to nonblocking I/O (%s)\n", strerror(errno));

		goto close_socket;
	}*/

	if (!SSL_set_fd(con->channel.control.ssl, con->channel.control.socket)) {
		console_error(_CLASS, _CONTROL, "failed to set I/O facility for SSL\n");

		goto close_socket;
	}

	/*
	 * it might be possible to switch to nonblocking I/O after the TLS handshake
	 * and switch back right before we initiate the shutdown procedure; this way
	 * we would avoid dealing with SSL_ERROR_WANT_READ/SSL_ERROR_WANT_WRITE outside
	 * of read/write calls
	 */

	/*if (SSL_connect(con->channel.control.ssl) <= 0) {
		printf("connection: error: failed to perform TLS/SSL handshake");

		goto close_socket;
	}*/

	do {
		int s = SSL_connect(con->channel.control.ssl);

		if (s > 0) break;

		fd_set rfds, wfds;

		int e = SSL_get_error(con->channel.control.ssl, s);

		if (e == SSL_ERROR_WANT_READ) {
			FD_ZERO(&rfds);
			FD_SET(con->channel.control.socket, &rfds);
			if (select(con->channel.control.socket + 1, &rfds, NULL, NULL, NULL) >= 0) {
				continue;
			}
		} else if (e == SSL_ERROR_WANT_WRITE) {
			FD_ZERO(&wfds);
			FD_SET(con->channel.control.socket, &wfds);
			if (select(con->channel.control.socket + 1, NULL, &wfds, NULL, NULL) >= 0) {
				continue;
			}
		}

		save_error_string(errno);

		console_lock();
		console_error(_CLASS, _CONTROL, "failed to perform SSL/TLS handshake ");

		switch (e) {
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				print_error("(failed to perform select on socket: %s)\n", get_error_string());
				goto unlock_console;
			case SSL_ERROR_ZERO_RETURN:
				print_error("(connection closed)\n");
				goto unlock_console;
			case SSL_ERROR_SSL:
				print_error("(protocol error)\n");
				goto unlock_console;
			case SSL_ERROR_SYSCALL:
				if (!ERR_peek_error()) {
					if (s == 0) {
						print_error("(protocol violating EOF)\n");
					} else if (s == -1) {
						print_error("(socket I/O error: %s)\n", get_error_string());
					}
				}
				goto unlock_console;
			default:
				print_error("(unsupported error code %i)\n", e);
				goto unlock_console;
		}
	} while (TRUE);

	/*if (!(con->channel.control.bio = BIO_new_ssl_connect(con->channel.control.ctx))) {
		ERR_error_string_n(ERR_get_error(), buf, 512);
		printf("connection: error: failed to create SSL BIO chain (%s)\n", buf);

		goto free_ctx;
	}

	SSL ssl;
	BIO_get_ssl(con->channel.control.bio, &ssl);
	SSL_set_mode(&ssl, SSL_MODE_AUTO_RETRY);

	BIO_set_conn_hostname(con->channel.control.bio, host);
	BIO_set_conn_port(con->channel.control.bio, port);

	if (BIO_do_connect(con->channel.control.bio) <= 0) {
		ERR_error_string_n(ERR_get_error(), buf, 512);
		printf("connection: error: failed to create TCP connection to %s:%s (%s)\n", host, port, buf);

		goto free_bio;
	}

	if (BIO_do_handshake(con->channel.control.bio) <= 0) {
		ERR_error_string_n(ERR_get_error(), buf, 512);
		printf("connection: error: failed to create SSL connection to %s:%s\n (%s)", host, port, buf);

		goto free_bio;
	}*/

	con->channel.control.connected = TRUE;

	con->channel.control.ping.n = 0;
	con->channel.control.ping.avg = 0.0;
	con->channel.control.ping.s = 0;
	con->channel.control.ping.var = 0.0;

	con->channel.control.tid = pthread_self();

	INIT_LIST_HEAD(&con->channel.control.queue);

	if (pipe(con->channel.control.notify)) {
		save_error_string(errno);
		console_error(_CLASS, _CONTROL, "failed to create notify pipe (%s)\n", get_error_string());

		goto close_socket;
	}

	if (connection_set_nonblock(con->channel.control.notify[0]) || connection_set_nonblock(con->channel.control.notify[1])) {
		console_error(_CLASS, _CONTROL, "failed to set notify pipe nonblocking (%s)\n", get_error_string());

		goto close_socket;
	}

	//pthread_mutex_init(&con->channel.control.queue_m, NULL);

	return 0;

	/*free_bio:
	BIO_free_all(con->channel.control.bio);*/

	unlock_console:
	console_unlock();

	close_socket:
	close(con->channel.control.socket);

	free_ssl:
	SSL_free(con->channel.control.ssl);

	free_ctx:
	SSL_CTX_free(con->channel.control.ctx);

	print_err:
	/*ERR_error_string_n(ERR_get_error(), buf, 512);
	printf(" (%s)\n", buf);*/
	if (ERR_peek_error()) {
		print_error("*** OpenSSL error queue:\n");
		ERR_print_errors_fp(console_get_error_file());
	}

	//con->retry = FALSE;

	return -1;
}

void control_close(struct connection *con) {
	if (!pthread_equal(pthread_self(), con->channel.control.tid)) return;

	//BIO_free_all(con->channel.control.bio);
	//SSL_shutdown(con->channel.control.ssl);
	do {
		int s = SSL_shutdown(con->channel.control.ssl);

		fd_set rfds, wfds;

		int e = SSL_get_error(con->channel.control.ssl, s);

		if (e == SSL_ERROR_WANT_READ) {
			FD_ZERO(&rfds);
			FD_SET(con->channel.control.socket, &rfds);
			if (select(con->channel.control.socket + 1, &rfds, NULL, NULL, NULL) >= 0) {
				continue;
			}
		} else if (e == SSL_ERROR_WANT_WRITE) {
			FD_ZERO(&wfds);
			FD_SET(con->channel.control.socket, &wfds);
			if (select(con->channel.control.socket + 1, NULL, &wfds, NULL, NULL) >= 0) {
				continue;
			}
		}

		break;
	} while (TRUE);

	// might need to shutdown()
	shutdown(con->channel.control.socket, SHUT_RDWR);

	close(con->channel.control.socket);

	SSL_free(con->channel.control.ssl);

	SSL_CTX_free(con->channel.control.ctx);

	con->channel.control.connected = FALSE;

	control_clear_queue(con);

	close(con->channel.control.notify[0]);
	close(con->channel.control.notify[1]);

	//pthread_mutex_destroy(&con->channel.control.queue_m);

	ERR_remove_state(0);
}

int control_send(struct connection *con, void *buf, int len) {
	/*int s, t;

	for (s = 0; s < len; s += t) {
		t = BIO_write(con->channel.control.bio, buf + s, len - s);

		if (t <= 0) {
			return -1;
		}
	}

	return s;*/

	/*int s = SSL_write(con->channel.control.ssl, buf, len);

	return s > 0 ? s : -1;*/

	if (pthread_equal(pthread_self(), con->channel.control.tid)) {
		int s, t = 0;

		fd_set rfds, wfds;

		do {
			ERR_clear_error();

			s = SSL_write(con->channel.control.ssl, buf + t, len - t);

			/* partial writes are disabled, so this is actually not necessary */
			if (s > 0) {
				t += s;

				continue;
			}

			int e = SSL_get_error(con->channel.control.ssl, s);

			if (e == SSL_ERROR_WANT_READ) {
				FD_ZERO(&rfds);
				FD_SET(con->channel.control.socket, &rfds);
				if (select(con->channel.control.socket + 1, &rfds, NULL, NULL, NULL) > 0) {
					continue;
				}
			} else if (e == SSL_ERROR_WANT_WRITE) {
				FD_ZERO(&wfds);
				FD_SET(con->channel.control.socket, &wfds);
				if (select(con->channel.control.socket + 1, NULL, &wfds, NULL, NULL) > 0) {
					continue;
				}
			} else if (e == SSL_ERROR_SYSCALL && s == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				FD_ZERO(&wfds);
				FD_SET(con->channel.control.socket, &wfds);
				if (select(con->channel.control.socket + 1, NULL, &wfds, NULL, NULL) > 0) {
					continue;
				}
			}

			return -1;
		} while (t < len);

		return t;
	} else {
		pthread_mutex_lock(&con->channel.control.m_queue);

		if (!con->channel.control.connected) {
			pthread_mutex_unlock(&con->channel.control.m_queue);

			return -1;
		}

		struct control_message *m = malloc(sizeof(struct control_message));

		m->buf = malloc(len);
		memcpy(m->buf, buf, len);
		m->len = len;

		list_add_tail(&m->l_messages, &con->channel.control.queue);

		char b = 0xFF;
		write(con->channel.control.notify[1], &b, 1);

		pthread_mutex_unlock(&con->channel.control.m_queue);

		return len;
	}
}

int control_recv(struct connection *con, void *buf, int len) {
	/*int r = BIO_read(con->channel.control.bio, buf, len);

	return r > 0 ? r : -1;*/

	/*int r = SSL_read(con->channel.control.ssl, buf, len);

	return r > 0 ? r : -1;*/
	
	if (!pthread_equal(pthread_self(), con->channel.control.tid)) return -1;

	int r, s, t = 0;

	fd_set rfds, wfds;

	if (!(r = SSL_pending(con->channel.control.ssl))) {
		struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

		FD_ZERO(&rfds);
		FD_SET(con->channel.control.socket, &rfds);
		FD_SET(con->channel.control.notify[0], &rfds);
		FD_SET(con->channel.voice.socket, &rfds);

		int nfds = max(con->channel.control.socket, con->channel.control.notify[0]);
		nfds = max(nfds, con->channel.voice.socket);
		nfds++;

		r = select(nfds, &rfds, NULL, NULL, &tv);

		if (FD_ISSET(con->channel.control.notify[0], &rfds)) {
			/*while (TRUE) {
				char b;
				if (read(con->channel.control.notify[0], &b, 1) <= 0) break;
			}*/
			r--;
		}

		if (FD_ISSET(con->channel.voice.socket, &rfds)) {
			r--;
		}
	}

	if (r > 0) {
		do {
			ERR_clear_error();

			s = SSL_read(con->channel.control.ssl, buf + t, len - t);

			if (s > 0) {
				t += s;

				continue;
			}

			int e = SSL_get_error(con->channel.control.ssl, s);

			if (e == SSL_ERROR_WANT_READ) {
				FD_ZERO(&rfds);
				FD_SET(con->channel.control.socket, &rfds);
				if (select(con->channel.control.socket + 1, &rfds, NULL, NULL, NULL) > 0) {
					continue;
				}
			} else if (e == SSL_ERROR_WANT_WRITE) {
				FD_ZERO(&wfds);
				FD_SET(con->channel.control.socket, &wfds);
				if (select(con->channel.control.socket + 1, NULL, &wfds, NULL, NULL) > 0) {
					continue;
				}
			} else if (e == SSL_ERROR_SYSCALL && s == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				FD_ZERO(&rfds);
				FD_SET(con->channel.control.socket, &rfds);
				if (select(con->channel.control.socket + 1, &rfds, NULL, NULL, NULL) > 0) {
					continue;
				}
			}

			if (s == 0) {
				console_message(_CLASS, _CONTROL, "connection shutdown by peer %s:%s\n", con->host, con->port);
			}

			return -1;
		} while (t < len);

		return t;
	}

	return r;
}

int voice_open(struct connection *con) {
	/*if ((con->channel.voice.socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("connection: error: failed to create udp socket (%s)\n", strerror(errno));

		return -1;
	}

	struct sockaddr_in local;
	memset(&local, 0, sizeof(struct sockaddr_in));

	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(0);

	if (bind(con->channel.voice.socket, (struct sockaddr *) &local, sizeof(struct sockaddr_in)) < 0) {
		printf("error: failed to bind udp socket (%s)\n", strerror(errno));

		close(con->channel.voice.socket);

		return -1;
	}

	memset(&con->channel.voice.remote, 0, sizeof(struct sockaddr_in));

	con->channel.voice.remote.sin_family = AF_INET;

	struct hostent *srv = gethostbyname(con->host); // deprecated
	if (!srv) {
		printf("error: failed to resolve hostname %s (%s)\n", con->host, strerror(errno));

		close(con->channel.voice.socket);

		return -1;
	}
	memcpy(&con->channel.voice.remote.sin_addr.s_addr, srv->h_addr, srv->h_length);

	con->channel.voice.remote.sin_port = htons(atoi(con->port));*/

	struct addrinfo *result = connection_resolve_host(NULL, "0", SOCK_DGRAM, AI_PASSIVE);
	if (!result) goto err;

	if (connection_init_socket(&con->channel.voice.socket, result, FALSE, bind)) {
		freeaddrinfo(result);

		console_error(_CLASS, _VOICE, "failed to bind a socket for UDP traffic to %s:%s (%s)\n", con->host, con->port, get_error_string());

		goto err;
	}

	freeaddrinfo(result);

	result = connection_resolve_host(con->host, con->port, SOCK_DGRAM, 0);
	if (!result) goto close_socket;

	memcpy(&con->channel.voice.remote.addr,  result->ai_addr, result->ai_addrlen);
	con->channel.voice.remote.len = result->ai_addrlen;

	freeaddrinfo(result);

	con->channel.voice.enabled = FALSE;
	con->channel.voice.transmit_position = FALSE;

	con->channel.voice.ping.n = 0;
	con->channel.voice.ping.avg = 0.0;
	con->channel.voice.ping.s = 0;
	con->channel.voice.ping.var = 0.0;

	con->channel.voice.crypt.init = FALSE;

	pthread_mutex_init(&con->channel.voice.m_crypt, NULL);

	con->channel.voice.connected = TRUE;

	return 0;

	close_socket:
	close(con->channel.voice.socket);

	err:
	return -1;
}

void voice_close(struct connection *con) {
	con->channel.voice.connected = FALSE;

	// might need to shutdown()
	shutdown(con->channel.voice.socket, SHUT_RDWR);

	close(con->channel.voice.socket);

	pthread_mutex_destroy(&con->channel.voice.m_crypt);
}

int voice_send(struct connection *con, unsigned char *buf, int len) {
	udp_buffer dgram;

	if (!con->channel.voice.connected) return -1;

	if (!con->channel.voice.crypt.init) return 0;

	pthread_mutex_lock(&con->channel.voice.m_crypt);

	crypt_encrypt(&con->channel.voice.crypt, buf, dgram, len);

	pthread_mutex_unlock(&con->channel.voice.m_crypt);

	int t = 0;

	do {
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(con->channel.voice.socket, &wfds);

		int r = select(con->channel.voice.socket + 1, NULL, &wfds, NULL, NULL);

		if (r <= 0) return -1;

		int s = sendto(con->channel.voice.socket, dgram + t, len + CRYPT_HEADER_SIZE - t, 0, &con->channel.voice.remote.addr, con->channel.voice.remote.len);

		if (s > 0) {
			t += s;

			continue;
		}

		if (s < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;

		return -1;

	} while (t < len);

	return t;
}

int voice_recv(struct connection *con, unsigned char *buf) {
	udp_buffer dgram;

	/*if (!con->channel.voice.crypt.init) {
		sleep(1); // meh
		return 0;
	}*/

	struct sockaddr remote;
	socklen_t remote_len = sizeof(struct sockaddr);

	struct timeval tv;

	fd_set rfds;
	FD_ZERO(&rfds);

	int nfds;

	int r;

	if (SSL_pending(con->channel.control.ssl)) {
		tv = (struct timeval) { .tv_sec = 0, .tv_usec = 0 };

		FD_SET(con->channel.voice.socket, &rfds);

		nfds = con->channel.voice.socket + 1;

		r = select(nfds, &rfds, NULL, NULL, &tv);
	} else {
		tv = (struct timeval) { .tv_sec = 1, .tv_usec = 0 };

		FD_SET(con->channel.voice.socket, &rfds);
		FD_SET(con->channel.control.socket, &rfds);
		FD_SET(con->channel.control.notify[0], &rfds);

		nfds = max(con->channel.voice.socket, con->channel.control.socket);
		nfds = max(nfds, con->channel.control.notify[0]);
		nfds++;

		r = select(nfds, &rfds, NULL, NULL, &tv);

		if (FD_ISSET(con->channel.control.socket, &rfds)) r--;

		if (FD_ISSET(con->channel.control.notify[0], &rfds)) r--;
	}

	if (r <= 0) return r;

	int n = recvfrom(con->channel.voice.socket, dgram, UDP_BUFFER_SIZE, 0, &remote, &remote_len);

	if (n < 0) {
		return -1;
	} else if (n == 0) {
		console_message(_CLASS, _VOICE, "connection shutdown by peer %s:%s\n", con->host, con->port);

		return -1;
	}

	if (connection_compare_hosts(&remote, remote_len, &con->channel.voice.remote.addr, con->channel.voice.remote.len, 0)) {
		return 0;
	}

	if (!con->channel.voice.crypt.init) {
		return 0;
	}

	if (n <= CRYPT_HEADER_SIZE) {
		return 0;
	}

	//pthread_mutex_lock(&con->channel.voice.m_crypt);

	if (!crypt_decrypt(&con->channel.voice.crypt, dgram, buf, n)) {
		if (timer_elapsed(&con->channel.voice.crypt.last_good) > 5000ULL) {
			if (timer_elapsed(&con->channel.voice.crypt.last_request) > 5000ULL) {
				timer_restart(&con->channel.voice.crypt.last_request);
				con->channel.voice.crypt.request = TRUE;
			}
		}

		//pthread_mutex_unlock(&con->channel.voice.m_crypt);

		return 0;
	}

	//pthread_mutex_unlock(&con->channel.voice.m_crypt);

	return n - CRYPT_HEADER_SIZE;
}

/*int connection_open(struct connection *con) {
	if (control_open(con) < 0) {
		printf("error: failed to establish control channel to %s:%s\n", con->host, con->port);

		return -1;
	}

	if (voice_open(con) < 0) {
		printf("error: failed to establish voice channel to %s:%s\n", con->host, con->port);

		control_close(con);

		return -1;
	}

	timer_restart(&con->timestamp);

	con->shutdown = FALSE;

	return 0;
}*/

/*void connection_shutdown(struct connection *con) {
	//control_close(con);
	
	//voice_close(con);

	con->shutdown = TRUE;
}*/
