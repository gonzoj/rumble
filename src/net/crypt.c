#include <arpa/inet.h>
#include <openssl/aes.h>
#include <string.h>

#include "crypt.h"
#include "../timer.h"
#include "../types.h"

void crypt_init(struct crypt *c, const unsigned char *key, const unsigned char *eiv, const unsigned char *div) {
	int i;
	for (i = 0; i < 0x100; i++) {
		c->decrypt_history[i] = 0;
	}
	memcpy(c->key, key, AES_BLOCK_SIZE);
	memcpy(c->encrypt_iv, eiv, AES_BLOCK_SIZE);
	memcpy(c->decrypt_iv, div, AES_BLOCK_SIZE);
	AES_set_encrypt_key(key, 128, &c->encrypt_key);
	AES_set_decrypt_key(key, 128, &c->decrypt_key);
	c->good = c->late = c->lost = c->resync = 0;
	timer_new(&c->last_good);
	timer_new(&c->last_request);
	c->request = FALSE;
	c->init = TRUE;
}

#if defined(__LP64__)

#define BLOCKSIZE 2
#define SHIFTBITS 63

typedef uint64_t subblock;

#define SWAP64(x) ({register uint64_t __out, __in = (x); __asm__("bswap %q0" : "=r"(__out) : "0"(__in)); __out;})

#define SWAPPED(x) SWAP64(x)

#else

#define BLOCKSIZE 4
#define SHIFTBITS 31

typedef uint32_t subblock;

#define SWAPPED(x) htonl(x)

#endif

typedef subblock keyblock[BLOCKSIZE];

#define HIGHBIT (1 << SHIFTBITS)

static void inline ZERO(subblock *block) {
	int i;
	for (i = 0; i < BLOCKSIZE; i++) {
		block[i] = 0;
	}
}

static void inline S2(subblock *block) {
	subblock carry = SWAPPED(block[0]) >> SHIFTBITS;
	int i;
	for (i = 0; i < BLOCKSIZE - 1; i++) {
		block[i] = SWAPPED((SWAPPED(block[i]) << 1) | (SWAPPED(block[i + 1]) >> SHIFTBITS));
	}
	block[BLOCKSIZE - 1] = SWAPPED((SWAPPED(block[BLOCKSIZE - 1]) << 1) ^ (carry * 0x87));
}

static void inline XOR(subblock *dst, const subblock *a, const subblock *b) {
	int i;
	for (i = 0; i < BLOCKSIZE; i++) {
		dst[i] = a[i] ^ b[i];
	}
}

static void inline S3(subblock *block) {
	subblock carry = SWAPPED(block[0]) >> SHIFTBITS;
	int i;
	for (i = 0; i < BLOCKSIZE - 1; i++) {
		block[i] ^= SWAPPED((SWAPPED(block[i]) << 1) | (SWAPPED(block[i + 1]) >> SHIFTBITS));
	}
	block[BLOCKSIZE - 1] ^= SWAPPED((SWAPPED(block[BLOCKSIZE - 1]) << 1) ^ (carry * 0x87));
}

static void crypt_ocb_encrypt(struct crypt *c, const unsigned char *plain, unsigned char *encrypted, unsigned int len, const unsigned char *nonce, unsigned char *tag) {
	keyblock checksum, delta, tmp, pad;

	AES_encrypt(nonce, (unsigned char *) delta, &c->encrypt_key);
	ZERO(checksum);

	while (len > AES_BLOCK_SIZE) {
		S2(delta);
		XOR(tmp, delta, (subblock *) plain);
		AES_encrypt((const unsigned char *) tmp, (unsigned char *) tmp, &c->encrypt_key);
		XOR((subblock *) encrypted, delta, tmp);
		XOR(checksum, checksum, (const subblock *) plain);
		len -= AES_BLOCK_SIZE;
		plain += AES_BLOCK_SIZE;
		encrypted += AES_BLOCK_SIZE;
	}

	S2(delta);
	ZERO(tmp);
	tmp[BLOCKSIZE - 1] = SWAPPED(len * 8);
	XOR(tmp, tmp, delta);
	AES_encrypt((const unsigned char *) tmp, (unsigned char *) pad, &c->encrypt_key);
	memcpy(tmp, plain, len);
	memcpy((unsigned char *) tmp + len, (const unsigned char *) pad + len, AES_BLOCK_SIZE - len);
	XOR(checksum, checksum, tmp);
	XOR(tmp, pad, tmp);
	memcpy(encrypted, tmp, len);

	S3(delta);
	XOR(tmp, delta, checksum);
	AES_encrypt((const unsigned char *) tmp, tag, &c->encrypt_key);
}

static void crypt_ocb_decrypt(struct crypt *c, const unsigned char *encrypted, unsigned char *plain, unsigned int len, const unsigned char *nonce, unsigned char *tag) {
	keyblock checksum, delta, tmp, pad;

	AES_encrypt(nonce, (unsigned char *) delta, &c->encrypt_key);
	ZERO(checksum);

	while (len > AES_BLOCK_SIZE) {
		S2(delta);
		XOR(tmp, delta, (const subblock *) encrypted);
		AES_decrypt((const unsigned char *) tmp, (unsigned char *) tmp, &c->decrypt_key);
		XOR((subblock *) plain, delta, tmp);
		XOR(checksum, checksum, (subblock *) plain);
		len -= AES_BLOCK_SIZE;
		plain += AES_BLOCK_SIZE;
		encrypted += AES_BLOCK_SIZE;
	}

	S2(delta);
	ZERO(tmp);
	tmp[BLOCKSIZE - 1] = SWAPPED(len * 8);
	XOR(tmp, tmp, delta);
	AES_encrypt((const unsigned char *) tmp, (unsigned char *) pad, &c->encrypt_key);
	memset(tmp, 0, AES_BLOCK_SIZE);
	memcpy(tmp, encrypted, len);
	XOR(tmp, tmp, pad);
	XOR(checksum, checksum, tmp);
	memcpy(plain, tmp, len);

	S3(delta);
	XOR(tmp, delta, checksum);
	AES_encrypt((const unsigned char *) tmp, (unsigned char *) tag, &c->encrypt_key);
}

void crypt_encrypt(struct crypt *c, const unsigned char *src, unsigned char *dst, unsigned int len) {
	unsigned char tag[AES_BLOCK_SIZE];

	int i;
	for (i = 0; i < AES_BLOCK_SIZE; i++) {
		if (++c->encrypt_iv[i]) break;
	}

	crypt_ocb_encrypt(c, src, dst + 4, len, c->encrypt_iv, tag);

	dst[0] = c->encrypt_iv[0];
	dst[1] = tag[0];
	dst[2] = tag[1];
	dst[3] = tag[2];
}

bool crypt_decrypt(struct crypt *c, const unsigned char *src, unsigned char *dst, unsigned int len) {
	if (len < 4) return FALSE;

	int i;

	unsigned int plain_len = len - 4;

	unsigned char save_iv[AES_BLOCK_SIZE];
	unsigned char iv = src[0];
	bool restore = FALSE;
	unsigned char tag[AES_BLOCK_SIZE];

	int lost = 0;
	int late = 0;

	memcpy(save_iv, c->decrypt_iv, AES_BLOCK_SIZE);

	if (((c->decrypt_iv[0] + 1) & 0xFF) == iv) {
		if (iv > c->decrypt_iv[0]) {
			c->decrypt_iv[0] = iv;
		} else if (iv < c->decrypt_iv[0]) {
			c->decrypt_iv[0] = iv;
			for (i = 1; i < AES_BLOCK_SIZE; i++) {
				if (++c->decrypt_iv[i]) break;
			}
		} else {
			return FALSE;
		}
	} else {
		int diff = iv - c->decrypt_iv[0];

		if (diff > 128) {
			diff -= 256;
		} else if (diff < -128) {
			diff += 256;
		}

		if (iv < c->decrypt_iv[0] && (diff > -30) && (diff < 0)) {
			late = 1;
			lost = -1;
			c->decrypt_iv[0] = iv;
			restore = TRUE;
		} else if (iv > c->decrypt_iv[0] && (diff > -30) && (diff < 0)) {
			late = 1;
			lost = -1;
			c->decrypt_iv[0] = iv;
			for (i = 1; i < AES_BLOCK_SIZE; i++) {
				if (c->decrypt_iv[i]--) break;
			}
			restore = TRUE;
		} else if (iv > c->decrypt_iv[0] && (diff > 0)) {
			lost = iv - c->decrypt_iv[0] - 1;
			c->decrypt_iv[0] = iv;
		} else if (iv < c->decrypt_iv[0] && (diff > 0)) {
			lost = 256 - c->decrypt_iv[0] + iv - 1;
			c->decrypt_iv[0] = iv;
			for (i = 1; i < AES_BLOCK_SIZE; i++) {
				if (++c->decrypt_iv[i]) break;
			}
		} else {
			return FALSE;
		}

		if (c->decrypt_history[c->decrypt_iv[0]] == c->decrypt_iv[1]) {
			memcpy(c->decrypt_iv, save_iv, AES_BLOCK_SIZE);
			
			return FALSE;
		}
	}

	crypt_ocb_decrypt(c, src + 4, dst, plain_len, c->decrypt_iv, tag);

	if (memcmp(tag, src + 1, 3) != 0) {
		memcpy(c->decrypt_iv, save_iv, AES_BLOCK_SIZE);

		return FALSE;
	}

	c->decrypt_history[c->decrypt_iv[0]] = c->decrypt_iv[1];

	if (restore) {
		memcpy(c->decrypt_iv, save_iv, AES_BLOCK_SIZE);
	}

	c->good++;
	c->late += late;
	c->lost += lost;

	timer_restart(&c->last_good);
	
	return TRUE;;
}
