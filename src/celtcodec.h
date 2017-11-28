#ifndef CELTCODEC_H_
#define CELTCODEC_H_

#include <celt/celt.h>

#include "list.h"
#include "types.h"

#define SAMPLE_RATE 48000

#define FRAMES_PER_SECOND 100

#define FRAME_SIZE (SAMPLE_RATE / FRAMES_PER_SECOND)

#define CHANNELS 1

enum {
	CELT_VERSION_0_11_0,
	CELT_VERSION_0_7_0,
	__CELT_UNSUPPORTED
};

enum {
	CELT_ALPHA,
	CELT_BETA
};

struct celtcodec {
	void *handle;
	char *dll;
	const char *version;
	int bitstream_version;
	CELTMode *cmode;
	struct list_head l_codecs;
	/* functions */
	CELTMode * (*__celt_mode_create)(celt_int32, int, int *);
	void (*__celt_mode_destroy)(CELTMode *);
	int (*__celt_mode_info)(CELTMode *, int, celt_int32 *);
	CELTEncoder * (*__celt_encoder_create)(CELTMode *, int, int *);
	CELTEncoder * (*encoder_create)(struct celtcodec *);
	void (*__celt_encoder_destroy)(CELTEncoder *);
	int (*__celt_encoder_ctl)(CELTEncoder *, int, ...);
	void *__celt_encode;
	int (*encode)(struct celtcodec *, CELTEncoder *, const celt_int16 *, unsigned char *, int);
	CELTDecoder * (*__celt_decoder_create)(CELTMode *, int, int *);
	CELTDecoder * (*decoder_create)(struct celtcodec *);
	void (*__celt_decoder_destroy)(CELTDecoder *);
	void *__celt_decode;
	int (*decode)(struct celtcodec *, CELTDecoder *, const unsigned char *, int, celt_int16 *);
	const char * (*__celt_strerror)(int);
};

#define encoder_destroy(ce) __celt_encoder_destroy(ce)
#define encoder_ctl(ce, r, a...) __celt_encoder_ctl(ce, r, ## a)
#define decoder_destroy(ce) __celt_decoder_destroy(ce)

struct celtcodec * celtcodec_new(int);

void celtcodec_free(struct celtcodec *);

bool celtcodec_load_all();

void celtcodec_free_all();

struct celtcodec * celtcodec_get(int);

struct list_head * celtcodec_list();

#endif /* CELTCODEC_H_ */
