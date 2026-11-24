/* Run the first logo's real eight effects through its complete 480-frame
 * scene. The model records are identifiers only: this exercises the original
 * work allocator, callbacks, key interpolation and scene endpoints, without
 * replacing any of those routines or needing copyrighted texture assets. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "player.h"

extern void at01_ef_set_all(void);

int main(void)
{
    int objects[8][7] = {{0}};
    int *pointers[8];
    OBJ_FILE file = {0};
    PLAYER_WORK *effects[8];
    VEC3 settled_positions[8];
    Sint32 settled_rotation[3];
    for (int index = 0; index < 8; ++index) pointers[index] = objects[index];
    file.obj = pointers;
    file.cnt = 8;
    obj_dm09 = &file;
    work_init();
    at01_ef_set_all();
    assert(work_cnt[W_SET] == 8);
    PLAYER_WORK *effect = work_top[W_SET];
    for (int index = 0; index < 8; ++index) {
        effects[index] = effect;
        assert(effect && effect->obj == pointers[index]);
        effect = effect->behind_w;
    }
    assert(!effect);

    for (int frame = 1; frame <= 480; ++frame) {
        work_list_move(W_SET);
        for (int index = 0; index < 8; ++index) {
            assert(effects[index]->timer1 <= 48);
            if (frame < 240) {
                assert(effects[index]->routine_0 == 0);
            } else {
                /* 49 keys define 48 intervals, each lasting five frames. */
                assert(effects[index]->routine_0 == 1);
                assert(effects[index]->timer0 == 0);
                assert(effects[index]->timer1 == 48);
            }
            if (frame == 240) settled_positions[index] = effects[index]->pos;
            if (frame > 240)
                assert(!memcmp(&settled_positions[index], &effects[index]->pos,
                               sizeof(VEC3)));
        }
        if (frame == 240) memcpy(settled_rotation, effects[0]->rot, sizeof settled_rotation);
        if (frame > 240)
            assert(!memcmp(settled_rotation, effects[0]->rot, sizeof settled_rotation));
        assert(work_cnt[W_SET] == 8);
    }
    for (int axis = 0; axis < 3; ++axis) assert(settled_rotation[axis] == 0);
    work_list_kill(W_SET);
    assert(work_cnt[W_SET] == 0 && free_cnt == WORK_MAX);
    obj_dm09 = NULL;
    puts("Native attract logo: all eight effects settle at frame 240 and hold through frame 480.");
    return 0;
}
