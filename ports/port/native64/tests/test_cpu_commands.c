/* Run the reconstructed computer command selector on ordinary high-address
 * native allocations. Rnd/rnd_init are linked from the real o_sub1.c unit;
 * unrelated screen routines in that unit are removed by dead-code stripping.
 * work.c and em_tbl.c supply the actual game globals/tables. No SDK or random
 * behavior is replaced by test stubs.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player.h"

extern void rnd_init(void);
extern Uint8 *em_rate_tbl[2];
extern Uint8 *em_rate_tbl2[3];
extern Sint8 *em_rnd_tbl[];

enum {
    DATA_BYTES = 2048,
    LEVELS_OFFSET = 128,
    COMMANDS_OFFSET = 256,
    RATES_OFFSET = 320,
    PICKS_OFFSET = 512,
    SCRIPTS_OFFSET = 1024,
    SCRIPT_STRIDE = 16,
    COMMAND_COUNT = 8
};

static unsigned char *make_data(int event_mode, unsigned record_index) {
    unsigned char *data = calloc(1, DATA_BYTES);
    assert(data != NULL);
    if (sizeof(void *) > 4) assert((uintptr_t)data > UINT32_MAX);
    CPU_DATA *records = (CPU_DATA *)data;
    CPU_DATA *record = &records[record_index];
    record->rate = RATES_OFFSET;
    record->cmd = COMMANDS_OFFSET;
    record->sel = event_mode ? PICKS_OFFSET : LEVELS_OFFSET;
    Sint32 *commands = (Sint32 *)(data + COMMANDS_OFFSET);
    for (unsigned command = 0; command < COMMAND_COUNT; ++command) {
        commands[command] = SCRIPTS_OFFSET + (Sint32)command * SCRIPT_STRIDE;
        memset(data + commands[command], (int)(0xa0 + command), SCRIPT_STRIDE);
    }
    for (unsigned level = 0; level < 8; ++level) {
        Sint32 *levels = (Sint32 *)(data + LEVELS_OFFSET);
        levels[level] = PICKS_OFFSET + (Sint32)level * 32;
        for (unsigned pick = 0; pick < 32; ++pick)
            data[levels[level] + pick] = (Uint8)((pick + level * 3) % COMMAND_COUNT);
    }
    memset(data + RATES_OFFSET, 32, 32);
    return data;
}

static void dirty_command_state(PLAYER_WORK *player) {
    player->cpu_cmd_timer = 77;
    player->cpu_guard_timer = 78;
    player->ev_try_flag = 0x1234;
    player->cpu_cancel_flag = 1;
    player->cpu_jump_flag = 1;
    player->cpu_fly_flag = 1;
    player->fly_flag = 1;
    player->cpu_lock_flag = 1;
    player->cpu_ex_flag = 1;
    player->cpu_air_jump_flag = 1;
    player->cpu_vcounter_cnt = 3;
    player->cpu_guard_back_cnt = 2;
    player->cpu_crouch_flag = 1;
    player->cpu_crouch_old = 1;
    player->cpu_sp_timer = 9;
    player->cpu_cmd_old = 0xab;
}

static void assert_command_cleared(const PLAYER_WORK *player) {
    assert(player->cpu_cmd_timer == 0);
    assert(player->cpu_guard_timer == 0);
    assert(player->ev_try_flag == 0);
    assert(player->cpu_cancel_flag == 0);
    assert(player->cpu_jump_flag == 0);
    assert(player->cpu_fly_flag == 0);
    assert(player->fly_flag == 0);
    assert(player->cpu_lock_flag == 0);
    assert(player->cpu_ex_flag == 0);
    assert(player->cpu_air_jump_flag == 0);
    assert(player->cpu_vcounter_cnt == 0);
    assert(player->cpu_guard_back_cnt == 0);
    assert(player->cpu_crouch_flag == 0);
    assert(player->cpu_crouch_old == 0);
    assert(player->cpu_sp_timer == 0);
    assert(player->cpu_cmd_old == 0);
}

static void init_script_sentinels(PLAYER_WORK *player, unsigned char *data) {
    CPU_SCRIPT *scripts[] = { &player->cpu_norm_sc, &player->cpu_range_sc,
                              &player->cpu_event_sc, &player->cpu_ex_sc };
    for (unsigned index = 0; index < 4; ++index) {
        scripts[index]->adrs = data + 1600 + index;
        scripts[index]->ofs = 0x1234;
        scripts[index]->no = 0xab;
        scripts[index]->cmd_cnt = 0xcd;
    }
}

static void test_selection(int kind, unsigned mode, unsigned level) {
    PLAYER_WORK player;
    memset(&player, 0, sizeof(player));
    const unsigned record_index = kind == 2 ? mode - 2 : 0;
    unsigned char *data = make_data(kind == 2, record_index);
    CPU_DATA *record = &((CPU_DATA *)data)[record_index];
    player.cpu_adrs[kind] = (CPU_DATA *)data;
    player.cpu_lv = (Uint8)level;
    init_script_sentinels(&player, data);
    dirty_command_state(&player);

    unsigned char *list = data + record->sel;
    if (kind != 2) list = data + ((Sint32 *)list)[level];
    rnd_init();
    const unsigned index = Rnd() & 31;
    const unsigned expected_pick = list[index];
    const Uint32 next_random = Rnd();
    unsigned char *expected_script = data + ((Sint32 *)(data + record->cmd))[expected_pick];
    rnd_init();
    em_cmd_sel(&player, (int)mode, kind);
    assert(Rnd() == next_random); /* Exactly one random value was consumed. */

    CPU_SCRIPT *scripts[] = { &player.cpu_norm_sc, &player.cpu_range_sc,
                              &player.cpu_event_sc, &player.cpu_ex_sc };
    for (unsigned script = 0; script < 4; ++script) {
        if ((int)script == kind) {
            assert(scripts[script]->adrs == expected_script);
            assert(*scripts[script]->adrs == 0xa0 + expected_pick);
            assert(scripts[script]->no == expected_pick);
            assert(scripts[script]->ofs == 0);
            assert(scripts[script]->cmd_cnt == 0);
        } else {
            assert(scripts[script]->adrs == data + 1600 + script);
            assert(scripts[script]->ofs == 0x1234);
            assert(scripts[script]->no == 0xab);
            assert(scripts[script]->cmd_cnt == 0xcd);
        }
    }
    assert_command_cleared(&player);
    free(data);
}

static void test_answer_selection(void) {
    PLAYER_WORK player, opponent;
    HIT_DATA hits[4];
    memset(&player, 0, sizeof(player));
    memset(&opponent, 0, sizeof(opponent));
    memset(hits, 0, sizeof(hits));
    unsigned char *data = make_data(1, 5);
    hits[2].cpu_answer_no = 5;
    opponent.hit_data_tbl = hits;
    player.em_atck_w = &opponent;
    player.em_atck_no = 2;
    player.cpu_answer_adrs = (CPU_DATA *)data;
    init_script_sentinels(&player, data);
    dirty_command_state(&player);
    rnd_init();
    const unsigned pick = data[PICKS_OFFSET + (Rnd() & 31)];
    const Uint32 next_random = Rnd();
    rnd_init();
    em_answer_sel(&player);
    assert(Rnd() == next_random);
    assert(player.cpu_event_sc.adrs == data + SCRIPTS_OFFSET + pick * SCRIPT_STRIDE);
    assert(player.cpu_event_sc.no == pick);
    assert(player.cpu_event_sc.ofs == 0 && player.cpu_event_sc.cmd_cnt == 0);
    assert(player.cpu_norm_sc.adrs == data + 1600);
    assert(player.cpu_range_sc.adrs == data + 1601);
    assert(player.cpu_ex_sc.adrs == data + 1603);
    assert_command_cleared(&player);
    free(data);
}

static void test_rates_and_modes(void) {
    PLAYER_WORK player;
    memset(&player, 0, sizeof(player));
    unsigned char *data = make_data(1, 3); /* Event mode five uses record three. */
    player.cpu_adrs[2] = (CPU_DATA *)data;
    cpu_level_work.level = 9;
    rnd_init();
    assert(em_rate_ck(&player, 5, 2));
    data[RATES_OFFSET + 9] = 0;
    assert(!em_rate_ck(&player, 5, 2));
    data[RATES_OFFSET + 9] = 17;
    rnd_init();
    const int expected = (Rnd() & 31) < 17;
    rnd_init();
    assert(em_rate_ck(&player, 5, 2) == expected);

    rnd_init();
    const unsigned pick = Rnd() & 31;
    rnd_init();
    assert(em_rate_ck(&player, 3, 2) == (pick < em_rate_tbl[0][9]));
    assert(!em_rate_ck(&player, 3, 0));
    rnd_init();
    assert(em_rate_ck(&player, 30, 2) == (pick < em_rate_tbl2[1][9]));
    assert(!em_rate_ck(&player, 30, 1));

    player.cpu_routine_1 = 5;
    assert(em_mode_set(&player, 5, 7) == 1);
    player.cpu_routine_1 = 0;
    player.ev_try_flag = 1U << 5;
    assert(em_mode_set(&player, 5, 7) == 1);
    assert(em_mode_clr(&player, 5) == 1 && player.ev_try_flag == 0);
    data[RATES_OFFSET + 9] = 0;
    assert(em_mode_set(&player, 5, 7) == 1);
    assert(player.cpu_routine_1 == 0 && player.ev_try_flag == (1U << 5));
    em_mode_clr(&player, 5);
    data[RATES_OFFSET + 9] = 32;
    player.cpu_routine_2 = player.cpu_routine_3 = 8;
    player.cpu_cmd_timer = 12;
    assert(em_mode_set(&player, 5, 7) == 0);
    assert(player.cpu_routine_1 == 5 && player.cpu_routine_2 == 0);
    assert(player.cpu_routine_3 == 0 && player.cpu_ev_lv == 7);
    assert_command_cleared(&player);
    free(data);
}

static void test_real_random_wait_tables(void) {
    PLAYER_WORK player;
    memset(&player, 0, sizeof(player));
    for (unsigned level = 0; level < 32; level += 31) {
        cpu_level_work.level = (Sint16)level;
        for (unsigned table = 0; table < 16; table += 2) {
            rnd_init();
            const unsigned index = Rnd() & 31;
            int expected = em_rnd_tbl[table][level] + em_rnd_tbl[table + 1][index];
            if (expected < 0) expected = 0;
            rnd_init();
            assert(em_rnd_get(&player, (int)table) == expected);
        }
    }
}

int main(void) {
    _Static_assert(sizeof(CPU_DATA) == 12, "CPU serialized offsets remain 32-bit");
    for (unsigned level = 0; level < 8; ++level) {
        test_selection(0, 0, level);
        test_selection(1, 1, level);
    }
    test_selection(2, 2, 0);
    test_selection(2, 5, 0);
    test_selection(2, 8, 0);
    test_answer_selection();
    test_rates_and_modes();
    test_real_random_wait_tables();
    puts("Native CPU commands: high-address offsets, script selection, rates, modes and real game RNG passed.");
    return 0;
}
