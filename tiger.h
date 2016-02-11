#ifndef TIGER_H
#define TIGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t word64;
typedef uint32_t word32;
typedef uint8_t byte;

extern void tiger(void *data, word64 length, word64 *hash);

#ifdef __cplusplus
}
#endif

#endif
