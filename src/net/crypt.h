#ifndef CRYPT_H_
#define CRYPT_H_

#include <openssl/aes.h>
#include <pthread.h>

#include "../timer.h"
#include "../types.h"

#define CRYPT_HEADER_SIZE 4

struct crypt {
	unsigned char key[AES_BLOCK_SIZE];
	unsigned char encrypt_iv[AES_BLOCK_SIZE];
	unsigned char decrypt_iv[AES_BLOCK_SIZE];
	unsigned char decrypt_history[0x100];
	AES_KEY encrypt_key;
	AES_KEY decrypt_key;
	bool init;
	uint32_t good;
	uint32_t late;
	uint32_t lost;
	uint32_t resync;
	struct timer last_good;
	struct timer last_request;
	bool request;
};

void crypt_init(struct crypt *, const unsigned char *, const unsigned char *, const unsigned char *);

void crypt_encrypt(struct crypt *, const unsigned char *, unsigned char *, unsigned int);

bool crypt_decrypt(struct crypt *, const unsigned char *, unsigned char *, unsigned int);

#endif
