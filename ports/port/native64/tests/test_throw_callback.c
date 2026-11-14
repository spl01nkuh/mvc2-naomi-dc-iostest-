/* Run the actual pl_tbl_catch_ck -> Ryu callback -> collision-box throw check.
   Only animation/work records are fixtures; no game/SDK callbacks are stubbed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player.h"

extern PLAYER_WORK *pl00_catch_ck(PLAYER_WORK *player);

int main(void)
{
    PLAYER_WORK *players = calloc(2, sizeof(*players));
    assert(players != NULL);
    const int high_pointer = (uintptr_t)players > UINT32_MAX;
    PLAYER_WORK *attacker = &players[0], *target = &players[1];
    HIT_BOX boxes[2] = {{0}, {0, 20, 0, 20}};
    FRAME_BOX frames[2] = {{0}};
    void (*callbacks[TBL_CATCH_CK + 1])(PLAYER_WORK *) = {0};
    callbacks[TBL_CATCH_CK] = (void (*)(PLAYER_WORK *))pl00_catch_ck;
    attacker->pl_tbl = callbacks;
    attacker->target_w = target;
    attacker->pl_stance = target->pl_stance = STANCE_STAND;
    attacker->atck_lever = KEY_FORWARD;
    attacker->atck_level = 1;
    attacker->atck_pk = 0;
    attacker->frame_box = &frames[0];
    target->frame_box = &frames[1];
    frames[0].atck_box2 = (Sint16)0x8001;
    frames[1].catch_box = 1;
    attacker->hit_box_tbl = target->hit_box_tbl = boxes;
    attacker->scale.x = attacker->scale.y = 1.0f;
    target->scale.x = target->scale.y = 1.0f;
    target->be_flag = target->dm_flag = 1;
    target->vital_new = 100;
    vs_sys_work.vs_st = VS_FIGHT;
    vs_sys_work.ctrl_flag = 0;

    assert(pl_tbl_catch_ck(attacker) == target);
    assert(attacker->catch_no == 1);
    attacker->atck_pk = 1;
    assert(pl_tbl_catch_ck(attacker) == target && attacker->catch_no == 0);
    vs_sys_work.ctrl_flag = CTRL_NO_THROW;
    assert(pl_tbl_catch_ck(attacker) == NULL);
    vs_sys_work.ctrl_flag = 0;
    target->pos.x = DOT_X(100.0f);
    assert(pl_tbl_catch_ck(attacker) == NULL);
    target->pos.x = 0;
    attacker->pl_stance = STANCE_CROUCH;
    assert(pl_tbl_catch_ck(attacker) == NULL);
    attacker->pl_stance = target->pl_stance = STANCE_AIR;
    attacker->pos.y = target->pos.y = DOT_Y(80.0f);
    assert(pl_tbl_catch_ck(attacker) == target && attacker->catch_no == 2);
    free(players);
    printf("Native throw dispatch: Ryu ground/air callbacks and real collision checks preserve pointers (%s allocation).\n",
           high_pointer ? "above 4 GiB" : "below 4 GiB");
    return 0;
}
