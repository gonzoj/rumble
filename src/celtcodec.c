#include <celt/celt.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "celtcodec.h"
#include "console.h"
#include "list.h"

#define _CLASS "celtcodec"

#define _RESOLVE(cc, s, l, n) cc->s = dlsym(cc->handle, n); if (!cc->s) { console_error(_CLASS, cc->version, "failed to resolve symbol %s in %s (%s)\n", n, l, dlerror()); goto err; }

static const char *s_version[] = {
	"0.11.0",
	"0.7.0"
};

static LIST_HEAD(celtcodecs);

static CELTEncoder * celt_encoder_create_wrapper(struct celtcodec *cc) {
	CELTEncoder *cencoder;

	int error;
	if (!(cencoder = cc->__celt_encoder_create(cc->cmode, CHANNELS, &error))) {
		console_error(_CLASS, cc->version, "failed to create encoder (%s)\n", cc->__celt_strerror(error));
	}

	return cencoder;
}

/*int celt07_celt_encode_wrapper(CELTEncoder **ce, const celt_int16 *pcm, unsigned char *compressed, int n) {
	struct celtcodec *cc = container_of(ce, struct celtcodec, cencoder);
	return ((int (*)(CELTEncoder *, const celt_int16 *, celt_int16 *, unsigned char *, int))cc->__celt_encode)(*ce, pcm, NULL, compressed, n);
}*/

static int celt070_celt_encode_wrapper(struct celtcodec *cc, CELTEncoder *cencoder, const celt_int16 *pcm, unsigned char *compressed, int n) {
	int c = ((int (*)(CELTEncoder *, const celt_int16 *, celt_int16 *, unsigned char *, int))cc->__celt_encode)(cencoder, pcm, NULL, compressed, n);
	if (c < 0) console_error(_CLASS, cc->version, "failed to encode audio frame (%s)\n", cc->__celt_strerror(c));
	return c;
}

static int celt011_celt_encode_wrapper(struct celtcodec *cc, CELTEncoder *cencoder, const celt_int16 *pcm, unsigned char *compressed, int n) {
	int c = ((int (*)(CELTEncoder *, const celt_int16 *, int, unsigned char *, int))cc->__celt_encode)(cencoder, pcm, FRAME_SIZE, compressed, n);
	if (c < 0) console_error(_CLASS, cc->version, "failed to encode audio frame (%s)\n", cc->__celt_strerror(c));
	return c;
}

static CELTDecoder * celt_decoder_create_wrapper(struct celtcodec *cc) {
	CELTDecoder *cdecoder;

	int error;
	if (!(cdecoder = cc->__celt_decoder_create(cc->cmode, CHANNELS, &error))) {
		console_error(_CLASS, cc->version, "failed to create decoder (%s)\n", cc->__celt_strerror(error));
	}

	return cdecoder;
}

static int celt070_celt_decode_wrapper(struct celtcodec *cc, CELTDecoder *cdecoder, const unsigned char *data, int len, celt_int16 *pcm) {
	int c = ((int (*)(CELTDecoder *, const unsigned char *, int, celt_int16 *))cc->__celt_decode)(cdecoder, data, len, pcm);
	if (c < 0) console_error(_CLASS, cc->version, "failed to decode audio frame (%s)\n", cc->__celt_strerror(c));
	return c;
}

static int celt011_celt_decode_wrapper(struct celtcodec *cc, CELTDecoder *cdecoder, const unsigned char *data, int len, celt_int16 *pcm) {
	int c = ((int (*)(CELTDecoder *, const unsigned char *, int, celt_int16 *, int))cc->__celt_decode)(cdecoder, data, len, pcm, FRAME_SIZE);
	if (c < 0) console_error(_CLASS, cc->version, "failed to decode audio frame (%s)\n", cc->__celt_strerror(c));
	return c;
}

struct celtcodec * celtcodec_new(int version) {
	struct celtcodec *cc = malloc(sizeof(struct celtcodec));
	cc->handle = NULL;
	cc->dll = NULL;

	char *sn_celt_encoder_create, *sn_celt_decoder_create;

	switch (version) {
		case CELT_VERSION_0_7_0:
			cc->version = s_version[version];
			sn_celt_encoder_create = "celt_encoder_create";
			sn_celt_decoder_create = "celt_decoder_create";
			cc->encode = celt070_celt_encode_wrapper;
			cc->decode = celt070_celt_decode_wrapper;
			break;
		case CELT_VERSION_0_11_0:
			cc->version = s_version[version];
			sn_celt_encoder_create = "celt_encoder_create_custom";
			sn_celt_decoder_create = "celt_decoder_create_custom";
			cc->encode = celt011_celt_encode_wrapper;
			cc->decode = celt011_celt_decode_wrapper;
			break;
		default:
			console_error(_CLASS, _NONE, "unsupported CELT version requested\n");

			goto err;
	}

	cc->dll = strdup("libcelt.so.");
	cc->dll = realloc(cc->dll, strlen(cc->dll) + strlen(cc->version) + 1);
	strcat(cc->dll, cc->version);

	cc->handle = dlopen(cc->dll, RTLD_LAZY);
	if (!cc->handle) {
		console_error(_CLASS, cc->version, "failed to load %s (%s)\n", cc->dll, dlerror());

		goto err;
	}

	_RESOLVE(cc, __celt_mode_create, cc->dll, "celt_mode_create")
	_RESOLVE(cc, __celt_mode_destroy, cc->dll, "celt_mode_destroy")
	_RESOLVE(cc, __celt_mode_info, cc->dll, "celt_mode_info")
	_RESOLVE(cc, __celt_encoder_create, cc->dll, sn_celt_encoder_create)
	_RESOLVE(cc, __celt_encoder_destroy, cc->dll, "celt_encoder_destroy")
	_RESOLVE(cc, __celt_encoder_ctl, cc->dll, "celt_encoder_ctl")
	_RESOLVE(cc, __celt_encode, cc->dll, "celt_encode")
	_RESOLVE(cc, __celt_decoder_create, cc->dll, sn_celt_decoder_create)
	_RESOLVE(cc, __celt_decoder_destroy, cc->dll, "celt_decoder_destroy")
	_RESOLVE(cc, __celt_decode, cc->dll, "celt_decode")
	_RESOLVE(cc, __celt_strerror, cc->dll, "celt_strerror")

	cc->encoder_create = celt_encoder_create_wrapper;
	cc->decoder_create = celt_decoder_create_wrapper;

	int error;

	cc->cmode = cc->__celt_mode_create(SAMPLE_RATE, FRAME_SIZE, &error);
	if (!cc->cmode) {
		console_error(_CLASS, cc->version, "failed to create valid CELT mode (%s)\n", cc->__celt_strerror(error));

		goto err;
	}

	if ((error = cc->__celt_mode_info(cc->cmode, CELT_GET_BITSTREAM_VERSION, (celt_int32 *) &cc->bitstream_version))) {
		console_error(_CLASS, cc->version, "failed to retrieve bitstream version (%s)\n", cc->__celt_strerror(error));

		cc->__celt_mode_destroy(cc->cmode);

		goto err;
	}

	list_add_tail(&cc->l_codecs, &celtcodecs);

	return cc;

	err:
	if (cc->handle) dlclose(cc->handle);

	if (cc->dll) free(cc->dll);

	free(cc);

	return NULL;
}

void celtcodec_free(struct celtcodec *cc) {
	list_del(&cc->l_codecs);

	cc->__celt_mode_destroy(cc->cmode);

	dlclose(cc->handle);

	free(cc->dll);

	free(cc);
}

bool celtcodec_load_all() {
	bool s = FALSE;


	struct celtcodec *cc;
	int i;
	for (i = 0; i < __CELT_UNSUPPORTED; i++) {
		cc = celtcodec_new(i);
		if (cc) {
			console_message(_CLASS, _NONE, "successfully initialized CELT %s\n", cc->version);
			s = TRUE;
		} else {
			console_error(_CLASS, _NONE, "failed to initialize CELT %s\n", s_version[i]);
		}
	}

	return s;
}

void celtcodec_free_all() {
	struct celtcodec *cc, *n;
	list_for_each_entry_safe(cc, n, &celtcodecs, l_codecs) {
		console_message(_CLASS, _NONE, "unloading CELT %s\n", cc->version);
		celtcodec_free(cc);
	}
}

struct celtcodec * celtcodec_get(int bitstream_version) {
	struct celtcodec *cc = NULL;

	if (bitstream_version == -1) {
		cc = list_first_entry(&celtcodecs, struct celtcodec, l_codecs);
	} else {
		list_for_each_entry(cc, &celtcodecs, l_codecs) {
			if (cc->bitstream_version == bitstream_version) break;
		}
	}

	return cc;
}

struct list_head * celtcodec_list() {
	return &celtcodecs;
}
