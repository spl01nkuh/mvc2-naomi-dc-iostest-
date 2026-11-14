/* Execute the original fight-word effect's private table/number routines.
   Drawing and unrelated scene paths are dead stripped, without SDK stubs. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../../src/ef12.c"

static void assert_spare_words(const PLAYER_WORK *effect)
{
    assert(effect->work1 == INT32_C(0x12345678));
    assert(effect->work2 == INT32_C(0x23456789));
    assert(effect->work3 == INT32_C(0x3456789a));
}

int main(void)
{
    PLAYER_WORK effect = {0}, player = {0};
    _Static_assert(sizeof(effect.work1) == 4 && sizeof(effect.work2) == 4 && sizeof(effect.work3) == 4,
                   "effect integer overlays stay 32-bit");
    _Static_assert(offsetof(PLAYER_WORK, work2) - offsetof(PLAYER_WORK, work1) == 4 &&
                   offsetof(PLAYER_WORK, work3) - offsetof(PLAYER_WORK, work2) == 4,
                   "spare integer words stay contiguous");
    _Static_assert(offsetof(PLAYER_WORK, native_work_ptr) >=
                   offsetof(PLAYER_WORK, unk_55d) + sizeof(effect.unk_55d),
                   "native pointer slots follow the original record");
    effect.work1 = INT32_C(0x12345678);
    effect.work2 = INT32_C(0x23456789);
    effect.work3 = INT32_C(0x3456789a);
    effect.native_work_ptr[0] = &player;

    Sys = &sys_work;
    Sys->vs_flag = 0;
    for (unsigned kind = 0; kind < 16; ++kind) {
        ef12_chr_clr(&effect, &player);
        EF12_KIND(&effect) = (Uint8)(kind | 0x80);
        player.cpu_flag = 0;
        ef12_chr_set(&effect, &player);
        assert(EF12_CHR0(&effect) == ef12_chr_tbl[kind]);
        assert(EF12_CHR1(&effect) == (ef12_se_tbl[kind] ? ef12_bonus_chr : NULL));
        assert(effect.native_work_ptr[0] == &player);
        assert_spare_words(&effect);
    }
    EF12_KIND(&effect) = 4;
    Sys->vs_flag = 1;
    ef12_chr_set(&effect, &player);
    assert(EF12_CHR1(&effect) == NULL);
    Sys->vs_flag = 0;
    player.cpu_flag = 1;
    ef12_chr_set(&effect, &player);
    assert(EF12_CHR1(&effect) == NULL);
    player.cpu_flag = 0;
    ef12_chr_set(&effect, &player);
    assert(EF12_CHR1(&effect) == ef12_bonus_chr);

    /* Non-voice words deliberately leave the bonus pointer unchanged. */
    EF12_KIND(&effect) = 0;
    ef12_chr_set(&effect, &player);
    assert(EF12_CHR1(&effect) == ef12_bonus_chr);

    const unsigned hits[] = {0, 7, 42, 999};
    /* The original unsigned-byte store keeps ' ' - '0' (0xf0) for blanks;
       the drawing routine treats any negative signed digit as blank. */
    const Uint8 digits[][3] = {{240, 240, 0}, {240, 240, 7}, {240, 4, 2}, {9, 9, 9}};
    for (unsigned index = 0; index < sizeof(hits) / sizeof(hits[0]); ++index) {
        memset(EF12_NUM(&effect), 0x5a, sizeof(EF12_NUM_W));
        EF12_HITS(&effect) = (Sint16)hits[index];
        ef12_hit_digit(&effect, &player);
        assert(memcmp(EF12_NUM(&effect)->num.hit, digits[index], 3) == 0);
        for (size_t byte = 3; byte < sizeof(EF12_NUM_W); ++byte)
            assert(((Uint8 *)EF12_NUM(&effect))[byte] == 0x5a);
        assert(EF12_CHR0(&effect) == ef12_hit_chr && EF12_CHR1(&effect) == NULL);
        assert_spare_words(&effect);
    }
    ef12_chr_clr(&effect, &player);
    assert(EF12_CHR0(&effect) == NULL && EF12_CHR1(&effect) == NULL);
    assert(effect.native_work_ptr[0] == &player);
    assert_spare_words(&effect);
    printf("Native fight words: actual table pointers, conditional bonus, digits and integer overlays passed (%s tables).\n",
           (uintptr_t)ef12_hit_chr > UINT32_MAX ? "above 4 GiB" : "below 4 GiB");
    return 0;
}
