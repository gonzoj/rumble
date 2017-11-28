#include <pthread.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "net/audio.h"
#include "celtcodec.h"
#include "config.h"
#include "net/connection.h"
#include "console.h"
#include "client.h"
#include "api/environment.h"
#include "handler.h"
#include "list.h"
#include "net/message.h"
#include "plugin.h"
#include "timer.h"
#include "types.h"
#include "version.h"

#include "net/protobuf/Mumble.pb-c.h"

static LIST_HEAD(clients);

static int _id = 0;

static void engine_send_ping(struct connection *con) {
	uint64_t ts = timer_elapsed(&con->timestamp);

	struct packet pkt = audio_new(UDP_TYPE_PING, 0);
		
	pkt.payload.timestamp = ts;

	audio_send(con, &pkt);

	MumbleProto__Ping msg = message_new(PING);

	message_set_optional(msg, timestamp, ts);
	message_set_optional(msg, good, con->channel.voice.crypt.good);
	message_set_optional(msg, late, con->channel.voice.crypt.late);
	message_set_optional(msg, lost, con->channel.voice.crypt.lost);
	message_set_optional(msg, resync, con->channel.voice.crypt.resync);
	message_set_optional(msg, udp_packets, con->channel.voice.ping.n);
	message_set_optional(msg, tcp_packets, con->channel.control.ping.n);

	if (con->channel.voice.ping.n) {
		message_set_optional(msg, udp_ping_avg, con->channel.voice.ping.avg);
		message_set_optional(msg, udp_ping_var, con->channel.voice.ping.var);
	}

	if (con->channel.control.ping.n) {
		message_set_optional(msg, tcp_ping_avg, con->channel.control.ping.avg);
		message_set_optional(msg, tcp_ping_var, con->channel.control.ping.var);
	}

	message_send(con, m_PING, &msg);
}

/*static void * voice_engine(void *arg) {
	struct client *c = (struct client *) arg;
	struct connection *con = c->con;

	if (voice_open(con)) {
		printf("engine: error: failed to establish voice channel to %s:%s\n", con->host, con->port);

		pthread_exit(NULL);
	} else {
		printf("engine: established voice channel to %s:%s\n", con->host, con->port);
	}

	while (con->channel.control.connected) {
		struct packet *pkt;

		int r;
		if ((r = audio_recv(con, &pkt)) < 0) break;

		if (r == 0) {
			if (con->channel.voice.crypt.request) {
				con->channel.voice.crypt.request = FALSE;

				MumbleProto__CryptSetup msg = message_new(CRYPT_SETUP);

				message_send(con, m_CRYPT_SETUP, &msg);
			}

			continue;
		}

		printf("voice packet received\n");

		if (c->handler[m_AUDIO].registered) c->handler[m_AUDIO].process(c, pkt);

		audio_free(pkt);
	}

	printf("engine: closing voice channel to %s:%s\n", con->host, con->port);
	voice_close(con);

	pthread_exit(NULL);
}*/

static void * engine(void *arg) {
	struct client *c = (struct client *) arg;
	struct connection *con = c->con;

	console_message("engine", c->username, "running\n");

	console_message("engine", c->username, "establishing control channel to %s:%s...\n", con->host, con->port);
	if (control_open(con)) {
		console_error("engine", c->username, "failed to establish control channel to %s:%s\n", con->host, con->port);

		goto engine_exit;
	}

	MumbleProto__Version vmsg = message_new(VERSION);

	message_set_optional(vmsg, version, _version);
	vmsg.release = _release;

	struct utsname sys;
	memset(&sys, 0, sizeof(struct utsname));

	if (!uname(&sys)) {
		vmsg.os = sys.sysname;
		vmsg.os_version = sys.release;
	}

	console_message("engine", c->username, "providing version information...\n");
	message_send(con, m_VERSION, &vmsg);

	MumbleProto__Authenticate amsg = message_new(AUTHENTICATE);

	amsg.username = c->username;
	amsg.password = c->password;

	struct celtcodec *cc;
	struct list_head *codecs = celtcodec_list();
	list_for_each_entry(cc, codecs, l_codecs) {
		message_add_repeated(amsg, celt_versions, cc->bitstream_version);
	}

	console_message("engine", c->username, "authenticating as %s...\n", c->username);
	message_send(con, m_AUTHENTICATE, &amsg);

	message_free_repeated(amsg, celt_versions);

	/*printf("engine: starting voice engine\n");
	pthread_t vtid;
	pthread_create(&vtid, NULL, voice_engine, (void *) c);*/

	console_message("engine", c->username, "establishing voice channel to %s:%s...\n", con->host, con->port);
	if (voice_open(con)) {
		console_warning("engine", c->username, "failed to establish voice channel to %s:%s\n", con->host, con->port);
		console_warning("engine", c->username, "UDP mode disabled\n");
	}

	struct timer ping = TIMER_INIT;

	while (!c->exit) {
		if (timer_is_elapsed(&ping, 5000000ULL)) {
			console_debug("engine", c->username, "pinging TCP/UDP channels\n");
			engine_send_ping(con);
		}

		struct profile *p;

		int r;

		if (con->channel.voice.connected) {

			struct packet *pkt;

			r = audio_recv(con, &pkt);

			if (r == 0) {
				if (con->channel.voice.crypt.request) {
					con->channel.voice.crypt.request = FALSE;

					MumbleProto__CryptSetup msg = message_new(CRYPT_SETUP);

					message_send(con, m_CRYPT_SETUP, &msg);
				}
			} else if (r > 0) {
				if (pkt->type == UDP_TYPE_PING) {
					console_debug("engine", c->username, "UDP ping received (timestamp: %llX)\n", pkt->payload.timestamp);
				} else {
					console_debug("engine", c->username, "voice packet received (session: %lli sequence: %lli)\n", pkt->payload.session, pkt->payload.sequence);
				}

				//if (c->handler[m_AUDIO].registered) c->handler[m_AUDIO].process(c, pkt);
				for_each_profile(p, c->profiles) {
					if (p->handler[m_AUDIO].registered) p->handler[m_AUDIO].process(c, pkt);
				}

				audio_free(pkt);
			}
		}

		void *msg;
		uint16_t type;

		//int r;
		if ((r = message_recv(con, &type, &msg)) < 0) break;

		control_flush_queue(con);

		if (r == 0) {
			continue;
		}

		console_debug("engine", c->username, "message %s received\n", s_message[type]);

		//if (c->handler[type].registered) c->handler[type].process(c, msg);
		for_each_profile(p, c->profiles) {
			if (p->handler[type].registered) p->handler[type].process(c, msg);
		}


		message_free(type, msg);
	}

	if (con->channel.voice.connected) {
		console_message("engine", c->username, "closing voice channel to %s:%s\n", con->host, con->port);
		voice_close(con);
	}

	console_message("engine", c->username, "closing control channel to %s:%s\n", con->host, con->port);
	control_close(con);

	//pthread_join(vtid, NULL);

	engine_exit:
	console_message("engine", c->username, "shutting down\n");

	c->running = FALSE;

	pthread_exit(&c->restart);
}

struct client * client_new(char *host, char *port, char *cert, char *user, char *pass, char *plugindir, char *packagedir, char *f_privilege) {
	struct client *c = malloc(sizeof(struct client));

	c->id = ++_id;
	
	c->con = connection_new(host, port, cert, settings.bitrate, settings.frames);

	c->celt.codec_version[0] = -1;
	c->celt.codec_version[1] = -1;
	c->celt.use = 0;
	pthread_mutex_init(&c->m_celt, NULL);

	c->username = user;
	if (!pass) {
		c->password = "";
	} else {
		c->password = pass;
	}

	c->exit = FALSE;

	c->restart = TRUE;

	INIT_LIST_HEAD(&c->profiles);

	handler_profile_bot(&c->profiles);
	handler_profile_api(&c->profiles);

	c->env = environment_new(c);

	INIT_LIST_HEAD(&c->plugins);

	c->plugindir = strdup(plugindir);
	c->packagedir = strdup(packagedir);
	plugin_load_all(&c->plugins, plugindir, packagedir);

	INIT_LIST_HEAD(&c->privileges);
	pthread_mutex_init(&c->m_privilege, NULL);

	c->f_privilege = strdup(f_privilege);
	controller_load_privileges(f_privilege, &c->privileges);

	pthread_create(&c->engine, NULL, engine, (void *) c);

	c->running = TRUE;

	list_add_tail(&c->l_clients, &clients);

	return c;
}

bool client_free(struct client *c) {
	//connection_shutdown(c->con);	
	list_del(&c->l_clients);

	c->exit = TRUE;

	bool *_restart;
	bool restart;

	pthread_join(c->engine, (void **) &_restart);
	restart = *_restart;

	pthread_mutex_destroy(&c->m_celt);

	connection_free(c->con);

	struct profile *p;
	for_each_profile(p, c->profiles) handler_profile_free(p);

	environment_free(c->env);

	plugin_unload_all(&c->plugins);
	free(c->plugindir);
	free(c->packagedir);

	controller_save_privileges(c->f_privilege, &c->privileges);
	free(c->f_privilege);

	controller_clear_privileges(&c->privileges);
	pthread_mutex_destroy(&c->m_privilege);

	free(c);

	return restart;
}

int client_get_celt_codec_version(struct client *c, int *use) {
	pthread_mutex_lock(&c->m_celt);

	int version;

	if (*use == -1) {
		version = c->celt.codec_version[c->celt.use];
		*use = c->celt.use;
	} else {
		version = c->celt.codec_version[*use];
	}

	pthread_mutex_unlock(&c->m_celt);

	return version;
}

void client_set_celt_codec_version(struct client *c, int alpha, int beta, int use) {
	pthread_mutex_lock(&c->m_celt);

	c->celt.codec_version[0] = alpha;
	c->celt.codec_version[1] = beta;
	c->celt.use = use;

	pthread_mutex_unlock(&c->m_celt);
}

struct list_head * client_list() {
	return &clients;
}
