#include <openssl/aes.h>
#include <stdlib.h>
#include <string.h>

#include "net/audio.h"
#include "celtcodec.h"
#include "client.h"
#include "net/connection.h"
#include "console.h"
#include "controller.h"
#include "net/crypt.h"
#include "handler.h"
#include "list.h"
#include "net/message.h"
#include "types.h"
#include "api/user.h"
#include "version.h"

#include "net/protobuf/Mumble.pb-c.h"

#define _CLASS "handler"

static void on_audio_message(struct client *c, struct packet *pkt) {
	if (pkt->type == UDP_TYPE_PING) {
		connection_update_ping(c->con, &c->con->channel.voice.ping, pkt->payload.timestamp);
	}
}

static void on_ping_message(struct client *c, MumbleProto__Ping *msg) {
	connection_update_ping(c->con, &c->con->channel.control.ping, msg->timestamp);

	if ((msg->good == 0 || c->con->channel.voice.crypt.good == 0) && c->con->channel.voice.enabled && timer_elapsed(&c->con->timestamp) > 20000000ULL) {
		c->con->channel.voice.enabled = FALSE;
		if (msg->good == 0 && c->con->channel.voice.crypt.good == 0) {
			console_warning(_CLASS, c->username, "UDP packets cannot be sent to or received from server\n");
		} else if (msg->good == 0) {
			console_warning(_CLASS, c->username, "UDP packets cannot be sent to server\n");
		} else {
			console_warning(_CLASS, c->username, "UDP packets cannot be received from server\n");
		}
		console_message(_CLASS, c->username, "switching to TCP mode\n");
	} else if (!c->con->channel.voice.enabled && msg->good > 3 && c->con->channel.voice.crypt.good > 3) {
		c->con->channel.voice.enabled = TRUE;
		console_message(_CLASS, c->username, "switching back to UDP mode\n");
	}
}

static void on_crypt_setup_message(struct client *c, MumbleProto__CryptSetup *msg) {
	if (msg->has_key && msg->has_client_nonce && msg->has_server_nonce) {
		if (msg->key.len == AES_BLOCK_SIZE && msg->client_nonce.len == AES_BLOCK_SIZE && msg->server_nonce.len == AES_BLOCK_SIZE) {
			crypt_init(&c->con->channel.voice.crypt, msg->key.data, msg->client_nonce.data, msg->server_nonce.data);

			c->con->channel.voice.enabled = TRUE;
		}
	} else if (msg->has_server_nonce) {
		if (msg->server_nonce.len == AES_BLOCK_SIZE) {
			c->con->channel.voice.crypt.resync++;
			//pthread_mutex_lock(&c->con->channel.voice.m_crypt);
			memcpy(c->con->channel.voice.crypt.decrypt_iv, msg->server_nonce.data, AES_BLOCK_SIZE);
			//pthread_mutex_unlock(&c->con->channel.voice.m_crypt);
		}
	} else {
		MumbleProto__CryptSetup rsp = message_new(CRYPT_SETUP);

		rsp.has_client_nonce = TRUE;
		rsp.client_nonce.data = malloc(AES_BLOCK_SIZE);
		rsp.client_nonce.len = AES_BLOCK_SIZE;
		pthread_mutex_lock(&c->con->channel.voice.m_crypt);
		memcpy(rsp.client_nonce.data, c->con->channel.voice.crypt.encrypt_iv, AES_BLOCK_SIZE);
		pthread_mutex_unlock(&c->con->channel.voice.m_crypt);

		message_send(c->con, m_CRYPT_SETUP, &rsp);

		free(rsp.client_nonce.data);
	}
}

static void on_version_message(struct client *c, MumbleProto__Version *msg) {
	console_lock();

	if (msg->has_version) {
		version_string s;
		console_message(_CLASS, c->username, "server protocol version: %s\n", version_to_string(msg->version, s));
	}
	if (msg->release) {
		console_message(_CLASS, c->username, "release: %s", msg->release);
		if (msg->os) {
			print_message(" (%s)", msg->os);
		}
		print_message("\n");
	}
	if (msg->os_version) {
		console_message(_CLASS, c->username, "OS: %s\n", msg->os_version);
	}

	console_unlock();
}

static void on_server_sync_message(struct client *c, MumbleProto__ServerSync *msg) {
	c->session = msg->session;
	if (msg->welcome_text) {
		console_message(_CLASS, c->username, "welcome message: %s\n", msg->welcome_text);
	}
	if (msg->has_max_bandwidth) {
		connection_set_max_bandwidth(c->con, msg->max_bandwidth);
	}
}

static void on_server_config_message(struct client *c, MumbleProto__ServerConfig *msg) {
	if (msg->welcome_text) {
		console_message(_CLASS, c->username, "welcome message: %s\n", msg->welcome_text);
	}
	if (msg->has_max_bandwidth) {
		connection_set_max_bandwidth(c->con, msg->max_bandwidth);
	}
}

static void on_codec_version_message(struct client *c, MumbleProto__CodecVersion *msg) {
	int alpha = msg->alpha;
	int beta = msg->beta;
	bool pref = msg->prefer_alpha;

	if (pref && celtcodec_get(alpha)) {
		client_set_celt_codec_version(c, alpha, -1, CELT_ALPHA);
	} else if (celtcodec_get(beta)) {
		client_set_celt_codec_version(c, -1, beta, CELT_BETA);
	} else if (!pref && celtcodec_get(alpha)) {
		client_set_celt_codec_version(c, alpha, -1, CELT_ALPHA);
	} else {
		client_set_celt_codec_version(c, -1, -1, CELT_ALPHA);
		console_warning(_CLASS, c->username, "unable to find matching CELT codecs with other clients\n");
	}
}

static void on_reject_message(struct client *c, MumbleProto__Reject *msg) {
	c->restart = FALSE;

	console_lock();

	console_error(_CLASS, c->username, "server connection rejected");
	if (msg->reason) print_error(": %s", msg->reason);
	print_error("\n");

	console_unlock();
}

static void on_user_remove_message(struct client *c, MumbleProto__UserRemove *msg) {
	console_lock();

	if (msg->session == c->session) {
		if (msg->ban) {
			console_message(_CLASS, c->username, "banned from server");
			if (msg->reason) print_message(" (%s)", msg->reason);
			print_message("\n");

			c->restart = FALSE;
		} else {
			console_message(_CLASS, c->username, "kicked from server");
			if (msg->reason) print_message(" (%s)", msg->reason);
			print_message("\n");
		}
	}

	console_unlock();
}

static void on_text_message(struct client *c, MumbleProto__TextMessage *msg) {
	int i;
	message_for_each_repeated(msg, session, i) {
		if (msg->session[i] == c->session) {
			struct user *u = user_get_by_session(msg->actor, &c->env->users);
			if (!u) return;

			console_message(_CLASS, c->username, "%s: %s\n", u->name, msg->message);

			if (msg->message[0] == '.') {
				controller_process_command(c, u, &msg->message[1]);
			}
		}
	}
}

void handler_profile_bot(struct list_head *profiles) {
	struct profile *p = malloc(sizeof(struct profile));

	handler_profile_new(p);

	handler_profile_register(p, m_AUDIO, on_audio_message);
	handler_profile_register(p, m_PING, on_ping_message);
	handler_profile_register(p, m_CRYPT_SETUP, on_crypt_setup_message);
	handler_profile_register(p, m_VERSION, on_version_message);
	handler_profile_register(p, m_SERVER_SYNC, on_server_sync_message);
	handler_profile_register(p, m_SERVER_CONFIG, on_server_config_message);
	handler_profile_register(p, m_CODEC_VERSION, on_codec_version_message);
	handler_profile_register(p, m_REJECT, on_reject_message);
	handler_profile_register(p, m_USER_REMOVE, on_user_remove_message);
	handler_profile_register(p, m_TEXT_MESSAGE, on_text_message);

	list_add_tail(&p->l_profiles, profiles);
}
