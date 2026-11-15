/*
 * sh_conv.h  a float made a Uint32 as SHC's code makes it, for the modules an ARM host wraps
 *
 * SHC takes 2^32 off a value of 2^31 or more and then truncates it as a signed int, so a negative wraps
 * (-1.5 gives 0xFFFFFFFF). ARM's own conversion stops a negative at 0.
 */

#ifndef SH_CONV_H
#define SH_CONV_H

#include "types.h"

static inline Uint32 sh_u32( Float f )
{
	if( !( 2147483648.0f > f ) ) f -= 4294967296.0f ;
	return ( Uint32 )( Sint32 )f ;
}

#endif
