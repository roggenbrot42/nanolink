#ifndef TYPES_H
#define TYPES_H

#include <inttypes.h>

typedef unsigned char octet;
typedef int (*callback_t)(void*); //generic callback type

#define OCTET_SIZE sizeof(octet) /* == 1 */
#define MAX_FRAME_SIZE 1024
#define LD_MAX_FRAME_SIZE 10

#endif

