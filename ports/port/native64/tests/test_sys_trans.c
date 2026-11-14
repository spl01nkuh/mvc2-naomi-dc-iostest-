/* Exercise the actual native variadic formatting and queue admission routines.
   Drawing-only functions are dead stripped; no font/SDK stubs are supplied. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../../src/sys_trans.c"

static void first_callback(void) {}
static void second_callback(void) {}

int main(void)
{
    void *pointer = malloc(1);
    assert(pointer != NULL);
    const int high_pointer = (uintptr_t)pointer > UINT32_MAX;
    SysTransReset();
    SysTransReset2();
    STrPrint(3, 4, 5, "%d %x %lld %.2f %s", -73, 0xface,
             INT64_C(4294967298), 1.25, "native");
    assert(str_cnt == 1 && str2_cnt == 0);
    assert(str_buf[0].x == 3 && str_buf[0].y == 4 && str_buf[0].type == 5);
    assert(strcmp(str_buf[0].str, "-73 face 4294967298 1.25 native") == 0);

    /* Consume more integer arguments than the ARM64 argument registers hold,
       while also passing floating-point and high native pointer arguments. */
    char expected[61];
    const char *format = "%d%d%d%d%d%d%d%d%d %.1f %.1f %p";
    snprintf(expected, sizeof(expected), format,
             1, 2, 3, 4, 5, 6, 7, 8, 9, 2.5, 3.5, pointer);
    STrPrint2(7, 8, -2, format,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 2.5, 3.5, pointer);
    assert(str2_cnt == 1 && str_cnt == 1);
    assert(str2_buf[0].x == 7 && str2_buf[0].y == 8 && str2_buf[0].type == -2);
    assert(strcmp(str2_buf[0].str, expected) == 0);

    memset(&str_buf[2], 0x5a, sizeof(str_buf[2]));
    STrPrint(1, 2, 3, "%080d", 7);
    assert(strlen(str_buf[1].str) == sizeof(str_buf[1].str) - 1);
    MES_WORK sentinel;
    memset(&sentinel, 0x5a, sizeof(sentinel));
    assert(memcmp(&str_buf[2], &sentinel, sizeof(sentinel)) == 0);

    for (int index = str_cnt; index < STR_MAX + 2; ++index)
        STrPrint(1, 1, 1, "%d", index);
    assert(str_cnt == STR_MAX);
    assert(strcmp(str_buf[STR_MAX - 1].str, "127") == 0);
    for (int index = str2_cnt; index < STR2_MAX + 2; ++index)
        STrPrint2(1, 1, 1, "%d", index);
    assert(str2_cnt == STR2_MAX);
    assert(strcmp(str2_buf[STR2_MAX - 1].str, "15") == 0);

    for (int index = 0; index < TRANS_MAX + 2; ++index) {
        SysTransSet(first_callback);
        SysTransSet2(second_callback);
    }
    assert(trans_cnt == TRANS_MAX && trans2_cnt == TRANS_MAX);
    assert(trans_tbl[TRANS_MAX - 1] == first_callback);
    assert(trans2_tbl[TRANS_MAX - 1] == second_callback);
    SysTransReset();
    assert(trans_cnt == 0 && str_cnt == 0);
    assert(trans2_cnt == TRANS_MAX && str2_cnt == STR2_MAX);
    SysTransReset2();
    assert(trans2_cnt == 0 && str2_cnt == 0);
    free(pointer);
    printf("Native late-frame queues: real variadic formatting, bounds and queue limits passed (%s pointer).\n",
           high_pointer ? "above 4 GiB" : "below 4 GiB");
    return 0;
}
