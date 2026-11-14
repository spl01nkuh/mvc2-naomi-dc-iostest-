/* Exercise actual team-clear routines and the native round/partner helpers used
   by pl_work_init/pl_change_set, without rooting all 59 character modules. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../../src/pl_sub.c"

static void assert_zero(const void *record, size_t size)
{
    const unsigned char *bytes = record;
    for (size_t index = 0; index < size; ++index) assert(bytes[index] == 0);
}

static void test_team_clear(void)
{
    unsigned char saved[6][sizeof(PLAYER_WORK)];
    const size_t kept_offset = offsetof(PLAYER_WORK, entry_routine_1);
    memset(pl_work, 0xa5, sizeof(pl_work));
    for (unsigned index = 0; index < 6; ++index) {
        pl_work[index].entry_routine_1 = (Sint8)(index + 1);
        pl_work[index].load_type = (Sint8)(10 + index);
        pl_work[index].load_color = (Sint8)index;
        pl_work[index].win_cnt = (Sint8)(20 + index);
        pl_work[index].cpu_flag = index & 1;
        pl_work[index].pl_data_adrs = (Sint32 *)&pl_work[(index + 1) % 6];
        for (unsigned slot = 0; slot < 3; ++slot)
            pl_work[index].native_work_ptr[slot] = &pl_work[(index + slot) % 6];
        memcpy(saved[index], &pl_work[index], sizeof(PLAYER_WORK));
    }
    pl_work_clear_1p();
    for (unsigned index = 1; index < 6; index += 2)
        assert(memcmp(saved[index], &pl_work[index], sizeof(PLAYER_WORK)) == 0);
    assert(vs_sys_work.win_cnt[0] == 20);
    pl_work_clear_2p();
    assert(vs_sys_work.win_cnt[1] == 0); /* A computer's saved wins do not carry. */
    for (unsigned index = 0; index < 6; ++index) {
        PLAYER_WORK *player = &pl_work[index];
        assert(player->pl_no == (index & 1));
        assert(player->work_no == index);
        assert(player->team_no == index / 2 && player->team_no_org == index / 2);
        assert(memcmp((unsigned char *)player + kept_offset,
                      saved[index] + kept_offset, sizeof(*player) - kept_offset) == 0);
        /* Only the original four identity fields are restored in the cleared
           fight region. Inspect all remaining bytes, including native CPU
           scripts and pointers beyond the former 0x4bc cutoff. */
        player->pl_no = player->work_no = player->team_no = player->team_no_org = 0;
        assert_zero(player, kept_offset);
    }
}

static void test_round_and_partner_spans(void)
{
    PLAYER_WORK player;
    unsigned char saved[sizeof(player)];
    Sint16 gauge = 123;
    const size_t round_start = offsetof(PLAYER_WORK, act_st);
    const size_t partner_start = offsetof(PLAYER_WORK, pl_new_lvbt);
    const size_t end = offsetof(PLAYER_WORK, gauge);
    memset(&player, 0xa5, sizeof(player));
    player.gauge = &gauge;
    player.load_type = 3;
    player.load_color = 2;
    memcpy(saved, &player, sizeof(saved));
    pl_round_state_clear(&player);
    assert(memcmp(&player, saved, round_start) == 0);
    assert_zero((unsigned char *)&player + round_start, end - round_start);
    assert(memcmp((unsigned char *)&player + end, saved + end, sizeof(player) - end) == 0);
    assert(player.gauge == &gauge && gauge == 123);

    memcpy(&player, saved, sizeof(player));
    pl_partner_state_clear(&player);
    assert(memcmp(&player, saved, partner_start) == 0);
    assert_zero((unsigned char *)&player + partner_start, end - partner_start);
    assert(memcmp((unsigned char *)&player + end, saved + end, sizeof(player) - end) == 0);
    assert(player.gauge == &gauge && gauge == 123);
}

static void test_system_game_region(void)
{
    struct {
        unsigned char before[16];
        SYSTEM_WORK system;
        unsigned char after[16];
    } fixture;
    unsigned char settings[SYS_HALF];
    _Static_assert(offsetof(SYSTEM_WORK, game_timer) == SYS_HALF,
                   "power-on settings retain their original 128-byte boundary");
    _Static_assert(offsetof(SYSTEM_WORK, tex_buff) >= SYS_HALF,
                   "the widened pointer belongs to per-game state");
    memset(&fixture, 0xa5, sizeof(fixture));
    memcpy(settings, &fixture.system, sizeof(settings));
    /* This is the exact length used by game/attract/demo reset call sites. */
    memset(&fixture.system.game_timer, 0, SYS_GAME_CLEAR_SIZE);
    assert(memcmp(settings, &fixture.system, sizeof(settings)) == 0);
    assert_zero(&fixture.system.game_timer, sizeof(fixture.system) - SYS_HALF);
    for (size_t index = 0; index < sizeof(fixture.before); ++index) {
        assert(fixture.before[index] == 0xa5);
        assert(fixture.after[index] == 0xa5);
    }
    assert(fixture.system.tex_buff == NULL && fixture.system.partner_free == 0);
}

int main(void)
{
    test_team_clear();
    test_round_and_partner_spans();
    test_system_game_region();
    puts("Native state clears: real team routines clear fight state and preserve entry/load/gauge/settings metadata.");
    return 0;
}
