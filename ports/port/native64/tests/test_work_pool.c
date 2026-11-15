/* Execute the original allocator and list routines with native-sized records.
   The renderer-dependent work_list_disp routine is excluded by dead stripping,
   rather than supplying a placeholder renderer. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player.h"

static int visited;
static PLAYER_WORK *seen[WORK_MAX];

static void observe(PLAYER_WORK *work)
{
    assert(visited < WORK_MAX);
    seen[visited++] = work;
}

static void release_during_move(PLAYER_WORK *work)
{
    observe(work);
    work_free(work);
}

static void expect_list(unsigned list, PLAYER_WORK *const *expected, size_t count)
{
    PLAYER_WORK *work = work_top[list];
    PLAYER_WORK *previous = NULL;
    size_t index;
    assert(work_cnt[list] == (Sint16)count);
    assert(work_top[list] == (count ? expected[0] : NULL));
    assert(work_end[list] == (count ? expected[count - 1] : NULL));
    for (index = 0; index < count; ++index) {
        assert(work == expected[index]);
        assert(work->before_w == previous);
        assert(work->work_kind == list);
        previous = work;
        work = work->behind_w;
    }
    assert(work == NULL);
}

static void test_native_slots(void)
{
    PLAYER_WORK *allocated[WORK_MAX];
    size_t index;
    assert(WORK_SIZE == sizeof(PLAYER_WORK));
    assert(sizeof(WORK_BUFF) == sizeof(PLAYER_WORK));
    assert(_Alignof(WORK_BUFF) >= _Alignof(PLAYER_WORK));
    assert(offsetof(PLAYER_WORK, act_st) > 0x1d0 || sizeof(void *) == 4);
    memset(work_buff, 0xa5, sizeof(work_buff));
    work_init();
    assert(free_cnt == WORK_MAX && work_rest_cnt == WORK_MAX - 10);
    for (index = 0; index < WORK_MAX; ++index) {
        allocated[index] = work_get2(NULL, W_EF, LIST_END);
        assert(allocated[index] == &work_buff[index].player);
        assert((uintptr_t)allocated[index] % _Alignof(PLAYER_WORK) == 0);
        assert(allocated[index]->master_w == NULL);
        assert(allocated[index]->cpu_ex_sc.adrs == NULL);
        for (size_t slot = 0; slot < 3; ++slot)
            assert(allocated[index]->native_work_ptr[slot] == NULL);
        assert(allocated[index]->act_st == 0);
        /* Touch fields beyond the old 464-byte limit. Each neighbor must keep
           its own values through the final record, with sanitizers enabled. */
        allocated[index]->act_st = (Uint8)(index + 1);
        allocated[index]->cpu_ex_sc.adrs = (Uint8 *)&work_buff[index];
        for (size_t slot = 0; slot < 3; ++slot)
            allocated[index]->native_work_ptr[slot] = &work_buff[(index + slot) % WORK_MAX];
    }
    assert(free_top == NULL && free_end == NULL && free_cnt == 0);
    assert(work_rest_cnt == -10);
    assert(work_get2(NULL, W_EF, LIST_END) == NULL);
    for (index = 0; index < WORK_MAX; ++index) {
        assert(allocated[index]->act_st == (Uint8)(index + 1));
        assert(allocated[index]->cpu_ex_sc.adrs == (Uint8 *)&work_buff[index]);
        for (size_t slot = 0; slot < 3; ++slot)
            assert(allocated[index]->native_work_ptr[slot] == &work_buff[(index + slot) % WORK_MAX]);
    }
    expect_list(W_EF, allocated, WORK_MAX);
    work_list_kill(W_EF);
    expect_list(W_EF, NULL, 0);
    assert(free_cnt == WORK_MAX && work_rest_cnt == WORK_MAX - 10);
    /* Recycling retains queue order and zeroes the whole native slot. */
    for (index = 0; index < WORK_MAX; ++index) {
        PLAYER_WORK *work = work_get2(NULL, W_SYS, LIST_END);
        assert(work == allocated[index]);
        assert(work->act_st == 0 && work->cpu_ex_sc.adrs == NULL);
        for (size_t slot = 0; slot < 3; ++slot)
            assert(work->native_work_ptr[slot] == NULL);
    }
    work_list_kill(W_SYS);
}

static void test_reserved_capacity(void)
{
    int index;
    work_init();
    for (index = 0; index < WORK_MAX - 10; ++index)
        assert(work_get(NULL, W_PLS, LIST_END) != NULL);
    assert(work_get(NULL, W_PLS, LIST_END) == NULL);
    assert(free_cnt == 10 && work_rest_cnt == 0);
    for (index = 0; index < 10; ++index)
        assert(work_get2(NULL, W_PLS, LIST_END) != NULL);
    assert(work_get2(NULL, W_PLS, LIST_END) == NULL);
    work_free(work_top[W_PLS]);
    assert(free_cnt == 1 && work_rest_cnt == -9);
    assert(work_get(NULL, W_PLS, LIST_END) == NULL);
    assert(work_get2(NULL, W_PLS, LIST_END) != NULL);
    work_list_kill(W_PLS);
    assert(free_cnt == WORK_MAX && work_rest_cnt == WORK_MAX - 10);
}

static void test_list_positions_and_move(void)
{
    PLAYER_WORK *a, *b, *c, *d, *e, *f;
    PLAYER_WORK *order[5];
    PLAYER_WORK *with_head[6];
    size_t index;
    work_init();
    a = work_get2(NULL, W_SHL, LIST_END);
    b = work_get2(NULL, W_SHL, LIST_TOP);
    c = work_get2(b, W_SHL, LIST_BEHIND);
    d = work_get2(a, W_SHL, LIST_BEFORE);
    e = work_get2(a, W_SHL, LIST_BEHIND);
    order[0] = b; order[1] = c; order[2] = d; order[3] = a; order[4] = e;
    expect_list(W_SHL, order, 5);
    f = work_get2(b, W_SHL, LIST_BEFORE);
    with_head[0] = f;
    for (index = 0; index < 5; ++index) with_head[index + 1] = order[index];
    expect_list(W_SHL, with_head, 6);
    work_free(f);
    expect_list(W_SHL, order, 5);
    visited = 0;
    for (index = 0; index < 5; ++index) order[index]->work_func = observe;
    work_list_move(W_SHL);
    assert(visited == 5);
    for (index = 0; index < 5; ++index) assert(seen[index] == order[index]);
    /* Free middle, head, and tail, then the remaining singleton. */
    work_free(d);
    order[2] = a; order[3] = e;
    expect_list(W_SHL, order, 4);
    work_free(b);
    order[0] = c; order[1] = a; order[2] = e;
    expect_list(W_SHL, order, 3);
    work_free(e);
    expect_list(W_SHL, order, 2);
    work_free(c);
    order[0] = a;
    expect_list(W_SHL, order, 1);
    work_free(a);
    expect_list(W_SHL, NULL, 0);
    assert(free_cnt == WORK_MAX);
    a = work_get2(NULL, W_DM, LIST_TOP);
    order[0] = a;
    expect_list(W_DM, order, 1);
    work_free(a);
    expect_list(W_DM, NULL, 0);
    /* A work can release itself during the original list's frame traversal. */
    for (index = 0; index < 5; ++index) {
        order[index] = work_get2(NULL, W_SYS, LIST_END);
        order[index]->work_func = release_during_move;
    }
    visited = 0;
    work_list_move(W_SYS);
    assert(visited == 5);
    for (index = 0; index < 5; ++index) assert(seen[index] == order[index]);
    expect_list(W_SYS, NULL, 0);
    assert(free_cnt == WORK_MAX);
}

static void test_priority_order(void)
{
    PLAYER_WORK *a, *b, *c;
    int index;
    work_init();
    disp_prio_clr();
    a = work_get2(NULL, W_SHL, LIST_END);
    b = work_get2(NULL, W_SHL, LIST_END);
    c = work_get2(NULL, W_PLS, LIST_END);
    a->disp_flag = b->disp_flag = c->disp_flag = 1;
    a->prio = b->prio = c->prio = 3;
    a->sub_prio = 4; b->sub_prio = -1; c->sub_prio = 4;
    disp_prio_set(a); disp_prio_set(b); disp_prio_set(c);
    assert(disp_prio_cnt[3] == 3);
    assert(disp_prio_w[3][0] == b && disp_prio_w[3][1] == a && disp_prio_w[3][2] == c);
    c->disp_flag = 0;
    disp_prio_set(c);
    assert(disp_prio_cnt[3] == 3);
    disp_prio_clr();
    assert(disp_prio_cnt[3] == 0);
    for (index = 0; index < PRIO_W_MAX + 5; ++index) disp_prio_set(a);
    assert(disp_prio_cnt[3] == PRIO_W_MAX);
    for (index = 0; index < PRIO_W_MAX; ++index) assert(disp_prio_w[3][index] == a);
    disp_prio_clr();
    work_list_kill(W_SHL);
    work_list_kill(W_PLS);
}

static void test_character_copies(void)
{
    PLAYER_WORK source = {0};
    PLAYER_WORK destination = {0};
    PLAYER_WORK before;
    CPU_DATA cpu_data[3] = {{0}};
    Sint32 commands[2] = {3, 7};
    Uint8 asset[16] = {0};
    const size_t part_begin = offsetof(PLAYER_WORK, tile_top);
    const size_t part_end = offsetof(PLAYER_WORK, atck_flag);
    const size_t anim_begin = offsetof(PLAYER_WORK, cg_prm);
    const size_t anim_end = offsetof(PLAYER_WORK, cg_adrs);
    assert(sizeof(CHAR_PART) == part_end - part_begin);
    assert(sizeof(CHAR_ANIM) == anim_end - anim_begin);
    source.tile_top = 42;
    source.disp_x = 36.5f;
    source.spr_no = 0x1234;
    source.char_no.w = 0x203;
    source.cell_adrs = asset + 1;
    source.spr_adrs = asset + 2;
    source.col_adrs = asset + 3;
    source.char_adrs = asset + 4;
    source.cpu_adrs[0] = &cpu_data[0];
    source.cpu_adrs[1] = &cpu_data[1];
    source.cpu_adrs[2] = &cpu_data[2];
    source.cpu_answer_adrs = cpu_data;
    source.cpu_ex_adrs = commands;
    destination.work3 = 0x76543210;
    destination.atck_flag = 12;
    destination.dm_flag = 17;
    destination.catch_w = &source;
    before = destination;
    CHAR_PART_COPY(&destination, &source);
    assert(destination.tile_top == 42 && destination.disp_x == 36.5f);
    assert(destination.spr_no == 0x1234 && destination.char_no.w == 0x203);
    assert(destination.cell_adrs == asset + 1 && destination.char_adrs == asset + 4);
    assert(destination.cpu_adrs[0] == &cpu_data[0]);
    assert(destination.cpu_adrs[1] == &cpu_data[1]);
    assert(destination.cpu_adrs[2] == &cpu_data[2]);
    assert(destination.cpu_answer_adrs == cpu_data && destination.cpu_ex_adrs == commands);
    assert(destination.work3 == 0x76543210);
    assert(destination.atck_flag == 12 && destination.dm_flag == 17);
    assert(destination.catch_w == &source);
    assert(memcmp(&before, &destination, part_begin) == 0);
    assert(memcmp((Uint8 *)&before + part_end, (Uint8 *)&destination + part_end,
                  sizeof(destination) - part_end) == 0);
    before = destination;
    CHAR_PART_COPY(&destination, &destination);
    assert(memcmp(&before, &destination, sizeof(destination)) == 0);

    source.cg_prm = 3;
    source.cg_type = 4;
    source.cg_ctr = 5;
    source.cg_end_flag = 6;
    source.spr_no = 0x3456;
    source.cg_prio = 7;
    source.cg_se = 8;
    source.cg_rival = 0x123;
    source.unk_152[0] = 9;
    source.unk_152[1] = 10;
    source.cg_adrs = (Sint8 *)asset + 1;
    destination.size_back = 27;
    destination.cg_adrs = (Sint8 *)asset + 2;
    destination.char_no.w = 0x456;
    before = destination;
    CHAR_ANIM_COPY(&destination, &source);
    assert(destination.cg_prm == 3 && destination.cg_type == 4);
    assert(destination.cg_ctr == 5 && destination.cg_end_flag == 6);
    assert(destination.spr_no == 0x3456 && destination.cg_prio == 7);
    assert(destination.cg_se == 8 && destination.cg_rival == 0x123);
    assert(destination.unk_152[0] == 9 && destination.unk_152[1] == 10);
    assert(destination.size_back == 27);
    assert(destination.cg_adrs == (Sint8 *)asset + 2 && destination.char_no.w == 0x456);
    assert(memcmp(&before, &destination, anim_begin) == 0);
    assert(memcmp((Uint8 *)&before + anim_end, (Uint8 *)&destination + anim_end,
                  sizeof(destination) - anim_end) == 0);
    before = destination;
    CHAR_ANIM_COPY(&destination, &destination);
    assert(memcmp(&before, &destination, sizeof(destination)) == 0);

    /* The documented packed body word must write exactly four size bytes,
       without aliasing an unrelated Uint32 object or changing its neighbors. */
    destination.team_top_w = &source;
    destination.cg_prm = 77;
    assert(BODY_SIZE_SET(&destination, UINT32_C(0x20202424)) == UINT32_C(0x20202424));
    assert(destination.size_top == 0x24 && destination.size_bottom == 0x24);
    assert(destination.size_front == 0x20 && destination.size_back == 0x20);
    assert(destination.team_top_w == &source && destination.cg_prm == 77);
}

int main(void)
{
    test_native_slots();
    test_reserved_capacity();
    test_list_positions_and_move();
    test_priority_order();
    test_character_copies();
    puts("Native work pool, lists, priorities, and character/animation copies passed.");
    return 0;
}
