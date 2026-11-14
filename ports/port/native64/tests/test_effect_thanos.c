/* Exercise the real Power Gem part-following routine with a full-width head
 * pointer. Source inclusion only exposes its static helper to this test;
 * unrelated effect/render entry points are removed by link-time dead stripping.
 * Animation uses the original ef_char_move(), not an SDK/test stub.
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../../../../src/shl36_00.c"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static void near_value(Float actual, Float expected)
{
    assert(fabsf(actual - expected) < 0.0001f);
}

static void test_part_follows_native_head(void)
{
    PLAYER_WORK head;
    PLAYER_WORK *part;
    PLAYER_WORK *master = &pl_work[0];
    memset(&head, 0, sizeof head);
    memset(master, 0, sizeof *master);
    work_init();
    part = work_get2(NULL, W_SHL, LIST_END);
    assert(part != NULL);
    assert(sizeof head.native_work_ptr[0] == sizeof(void *));
    /* A stack record gives the read path a genuine high pointer even when a
       host's non-PIE test executable places its global pool below 4 GiB. */
    if (sizeof(void *) > 4 && (uintptr_t)&head > UINT32_MAX)
        puts("Coverage: the original part routine received a head above 4 GiB.");
    else
        puts("Coverage note: this host did not place the head above 4 GiB.");
    part->native_work_ptr[2] = &head;
    part->native_work_ptr[0] = master;
    CHAR_PART_COPY(part, master);
    assert(part->native_work_ptr[2] == &head);
    assert(part->native_work_ptr[0] == master);
    /* The head's work3 remains an integer ending flag; only the part stores a
       head reference. Their roles must stay independent on the native ABI. */
    head.work3 = 1;
    head.routine_0 = 1;
    head.disp_flag = 1;
    head.pos.x = 120.0f;
    head.pos.y = 240.0f;
    head.scale.x = 1.5f;
    head.scale.y = 1.75f;
    head.scale.z = 1.0f;
    part->obj_flag = 16;
    part->work1 = 8;
    part->pos.z = 17.0f;
    part->cg_ctr = 2; /* Real ef_char_move decrements without fetching a frame. */

    shl36_00_part_move(part, master);
    near_value(part->pos.x, 160.0f);
    near_value(part->pos.y, 270.0f);
    near_value(part->pos.z, 17.0f);
    assert(part->disp_flag == 1 && part->cg_ctr == 1);
    assert(part->scale.x == head.scale.x && part->scale.y == head.scale.y);
    assert(part->native_work_ptr[2] == &head && head.work3 == 1);

    /* Facing reverses the horizontal offset; hit stop prevents the real
       animation step while geometry and visibility still follow the head. */
    part->disp_dir = 1;
    part->cg_ctr = 2;
    head.hit_stop = 3;
    head.disp_flag = 0;
    shl36_00_part_move(part, master);
    near_value(part->pos.x, 80.0f);
    near_value(part->pos.y, 270.0f);
    assert(part->disp_flag == 0 && part->cg_ctr == 2);

    /* The actual end transition runs when the head has left its active state. */
    part->routine_0 = 1;
    part->disp_flag = 1;
    head.routine_0 = 2;
    shl36_00_part_move(part, master);
    assert(part->routine_0 == 2 && part->disp_flag == 0);
    assert(part->cg_ctr == 2 && part->native_work_ptr[2] == &head);
    work_free(part);
    assert(free_cnt == WORK_MAX);
}

static void test_pointer_slots_reset_with_real_pool(void)
{
    PLAYER_WORK head;
    PLAYER_WORK *part;
    work_init();
    part = work_get2(NULL, W_SHL, LIST_END);
    assert(part != NULL);
    for (unsigned i = 0; i < 3; ++i) part->native_work_ptr[i] = &head;
    work_free(part);
    /* Reinitialization is the real game's scene reset, and must clear the
       appended pointer storage as well as the old arcade-sized prefix. */
    work_init();
    part = work_get2(NULL, W_SHL, LIST_END);
    assert(part != NULL);
    for (unsigned i = 0; i < 3; ++i) assert(part->native_work_ptr[i] == NULL);
    work_free(part);
}

int main(void)
{
    test_part_follows_native_head();
    test_pointer_slots_reset_with_real_pool();
    puts("Native Thanos part follows its head with original animation/pool routines.");
    return 0;
}
