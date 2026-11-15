/*
 * pl_char_host.c  pl_char.c for a host build, its lz_unpack through the port's cache of unpacked cells (nl_lz.c)
 */

#define lz_unpack lz_unpack_game
#include "pl_char.c"
#undef lz_unpack

int nl_lz_unpack(const void* src, int size, void* dst);
void nl_lz_check(const void* dst);

int lz_unpack(Sint8* src, int size, Uint8* dst)
{
	int r = nl_lz_unpack(src, size, dst);

	if (r == 1) return size;
	size = lz_unpack_game(src, size, dst);
	if (r == 2) nl_lz_check(dst);
	return size;
}
