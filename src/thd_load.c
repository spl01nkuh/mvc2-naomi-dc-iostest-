/* The loads of a file of textures and of a file the cache must see. */
#include "player.h"
#ifdef HOST_NATIVE64
#include "system_textures.h"
#endif

extern int file_load( int no, void *adrs, int flag );

/****************************************************************/
/*			Load a file of textures, and turn its addresses		*/
/*			into pointers; returns its size						*/
/****************************************************************/
int thd_file_load( int no, void *adrs )
{
#ifdef HOST_NATIVE64
	int size = file_load( no, adrs, 1 ) ;
	if( size <= 0 || !mvc2_system_textures_load( no, adrs, ( size_t )size ) ) return 0 ;
	return size ;
#else
	NLthd *thd = adrs ;
	int size ;
	int i ;

	size = file_load( no, adrs, 1 ) ;
	for( i = 0 ; ( Sint32 )thd[i].address > 0 ; i ++ ){
		thd[i].address = ( Uint8 * )thd[i].address + ( Sint32 )adrs ;
	}
	return size ;
#endif
}

/****************************************************************/
/*			Load a file and make the cache see it; returns		*/
/*			its size											*/
/****************************************************************/
int file_load1( int no, void *adrs )
{
	int size ;

	size = file_load( no, adrs, 1 ) ;
	return size ;
}

/*==============================================================*/
/*	End of File.												*/
/*==============================================================*/
/* "thd_load.c" End of File	*/
