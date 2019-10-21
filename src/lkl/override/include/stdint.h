#ifndef __LKL_STDINT_H__
#define __LKL_STDINT_H__

#include <linux/types.h>

#define UINT8_C(c)  c
#define UINT16_C(c) c
#define UINT32_C(c) c ## U
#define UINT64_C(c) c ## UL
#define UINT16_MAX U16_MAX

#endif
