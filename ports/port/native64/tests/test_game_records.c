/* Run the actual model helpers on a 64-bit allocation. These routines walk
   binary material records and previously truncated their pointers to int. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"

extern void obj_mat_count(int *obj, Sint32 *num);
extern void obj_blend_set(int *obj, Sint32 src, Sint32 dst);
extern void obj_mat_save(int *obj, Sint32 *buf);
extern void obj_mat_load(int *obj, Sint32 *buf);
extern void obj_uv_clear(int *obj);
extern void obj_argb_set(int *obj, Float a, Float r, Float g, Float b);

int main(void)
{
    enum { HEADER = 24, MATERIAL = 80, VERTICES = 32, STRIDE = MATERIAL + VERTICES };
    unsigned char *memory = calloc(1, HEADER + 2 * STRIDE + 4);
    Uint32 *first;
    Uint32 *second;
    Sint32 count = 0;
    Sint32 saved[8];
    assert(memory != NULL);
    assert(sizeof(Sint32) == 4 && sizeof(Uint32) == 4);
    assert(sizeof(Sint16) == 2 && sizeof(Uint16) == 2);
    assert(sizeof(Float) == 4);
    assert(sizeof(CPU_DATA) == 12);
    assert(sizeof(SW_WORK) == 20);
    assert(sizeof(HIT_DATA) == 28);
    assert(sizeof(CHAR_DATA) == 20);
    /* LP64 test coverage must exercise addresses that a 32-bit cast loses. */
    if (sizeof(void *) > 4) assert((uintptr_t)memory > UINT32_MAX);
    first = (Uint32 *)(memory + HEADER);
    second = (Uint32 *)(memory + HEADER + STRIDE);
    first[0] = second[0] = 0x02000001;
    first[1] = 0x11223344;
    second[1] = 0x55667788;
    first[2] = second[2] = 0x00018000;
    first[19] = second[19] = VERTICES;
    obj_mat_count((int *)memory, &count);
    assert(count == 2);
    obj_blend_set((int *)memory, 3, 4);
    assert(first[2] == 0x70018000 && second[2] == 0x70018000);
    obj_mat_save((int *)memory, saved);
    assert((Uint32)saved[1] == 0x11223344 && (Uint32)saved[5] == 0x55667788);
    first[1] = second[1] = 0;
    obj_mat_load((int *)memory, saved);
    assert(first[1] == 0x11223344 && second[1] == 0x55667788);
    obj_uv_clear((int *)memory);
    assert(first[2] == 0x70000000 && second[2] == 0x70000000);
    obj_argb_set((int *)memory, 1.0f, 0.5f, 0.25f, -0.0f);
    assert(first[11] == UINT32_C(0x3f800000) && second[11] == first[11]);
    assert(first[12] == UINT32_C(0x3f000000) && second[12] == first[12]);
    assert(first[13] == UINT32_C(0x3e800000) && second[13] == first[13]);
    assert(first[14] == UINT32_C(0x80000000) && second[14] == first[14]);
    free(memory);
    puts("Native fixed-width records and 64-bit model traversal passed.");
    return 0;
}
