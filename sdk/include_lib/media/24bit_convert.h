#ifndef _24BIT_CONVERT_H_
#define _24BIT_CONVERT_H_

#include "system/includes.h"

void threebyte_24bit_to_16bit(int *in, short *out, unsigned int total_point);
void threebyte_24bit_to_fourbyte_24bit(int *in, int *out, unsigned int total_point);
void fourbyte_24bit_to_threebyte_24bit(int *in, int *out, unsigned int total_point);
void _16bit_to_threebyte_24bit(short *in, int *out, unsigned int total_point);

#endif
