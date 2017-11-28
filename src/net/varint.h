#ifndef VARINT_H_
#define VARINT_H_

#include "../types.h"

int varint_decode(unsigned char *, uint64_t *);

int varint_encode(uint64_t, unsigned char **);

#endif /* VARINT_H_ */
