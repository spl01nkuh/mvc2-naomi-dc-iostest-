#include <assert.h>

#include "../../../../src/sys_mode.c"

static SYSTEM_WORK system_work;
static Sint32 queue_busy;
static int demo_init_calls;
SYSTEM_WORK *Sys = &system_work;

Sint32 load_ck(void)
{
    return queue_busy;
}

void mes_font_disp(const MES_WORK *message)
{
    (void)message;
}

void demo_init(void)
{
    ++demo_init_calls;
    assert(!queue_busy);
}

int main(void)
{
    Sys->scene_no = 1;
    Sys->task_step = 4;
    Sys->timer0 = COPYRIGHT_TIME - 24;
    queue_busy = 1;
    for (int frame = 0; frame < 3; ++frame) {
        attract_copyright_move();
        assert(Sys->timer0 == COPYRIGHT_TIME - 24);
        assert(demo_init_calls == 0);
    }

    queue_busy = 0;
    attract_copyright_move();
    assert(Sys->timer0 == COPYRIGHT_TIME - 25);
    assert(demo_init_calls == 1);

    while (Sys->timer0 > 1)
        attract_copyright_move();

    queue_busy = 1;
    attract_copyright_move();
    assert(Sys->task_step == 4 && Sys->scene_no == 1 && Sys->timer0 == 1);

    queue_busy = 0;
    attract_copyright_move();
    assert(Sys->task_step == 5 && Sys->scene_no == 0 && Sys->timer0 == 0);
    assert(demo_init_calls == 1);
    return 0;
}
