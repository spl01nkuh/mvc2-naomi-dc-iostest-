/* The SH-4 assembly source src/spr_tbl.src contains these complete tables.
   Keep the native definitions out of the original and existing 32-bit links,
   whose assembler/gen_host paths supply the same symbols. No ROM image is
   needed to reconstruct them. test_game_tables.py checks all entries against
   the original .DATA.W declarations rather than a second hand-copied fixture. */
#include "game_tables.h"

Sint16 spr_cnt_tbl[MVC2_NATIVE_FIGHTER_COUNT] = {
    441, 426, 451, 943, 1198, 825, 866, 562, 920, 941,
    671, 952, 1124, 511, 939, 770, 1107, 793, 741, 681,
    979, 546, 799, 681, 490, 377, 386, 467, 1022, 1026,
    525, 1392, 931, 318, 920, 267, 482, 541, 353, 407,
    601, 336, 1174, 576, 1101, 661, 1113, 663, 1242, 738,
    581, 1081, 392, 909, 710, 884, 730, 571, 969
};

Sint16 spr_set_cnt_tbl[MVC2_NATIVE_FIGHTER_COUNT] = {
    61, 17, 38, 71, 43, 66, 97, 21, 45, 127,
    15, 21, 159, 52, 70, 104, 261, 53, 24, 31,
    45, 51, 27, 40, 38, 37, 16, 8, 77, 31,
    69, 84, 42, 34, 50, 0, 9, 23, 25, 39,
    48, 21, 66, 52, 106, 32, 91, 102, 161, 36,
    13, 75, 39, 74, 118, 36, 81, 21, 254
};
