/*
 * set05_05_host.c  set05_05.c for a host build
 *
 * set05_05.c declares spline_weight (set01_02.c's) with the float first, where the definition takes it
 * last. The SH-4 and the ARM pass floats and pointers in registers of their own, so the call is the same
 * either way; x86 passes them on the stack in order, and spline_weight then stores through the float. So
 * on x86 set05_05.c calls a routine that takes them in its order.
 */

#include "types.h"

#define spline_weight set05_spline_weight
#include "set05_05.c"
#undef spline_weight

extern void spline_weight( Float *w0, Float *w1, Float *w2, Float t ) ;

void set05_spline_weight( Float t, Float *w0, Float *w1, Float *w2 )
{
	spline_weight( w0, w1, w2, t ) ;
}
