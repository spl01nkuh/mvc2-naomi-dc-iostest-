/*
 * restart.c  Starting a program again, or the other one: each layer's .data (between span_begin.c and
 * span_end.c) is copied before it first runs and put back, and its .bss cleared
 */

#include <stdlib.h>
#include <string.h>

#include "naomi_host.h"

extern char host_span_data_begin[], host_span_data_end[], host_span_bss_begin[], host_span_bss_end[];

static const HOST_SPAN host_span = { host_span_data_begin, host_span_data_end, host_span_bss_begin, host_span_bss_end };

static struct {
    const HOST_SPAN* span;
    char* data;
} kept[3];
static int nkept;

static void keep(const HOST_SPAN* s) {
    size_t n = (size_t)(s->data_end - s->data);
    char* p;
    int i;
    for (i = 0; i < nkept; i++) {
        if (kept[i].span == s) {
            return;
        }
    }
    p = host_try_malloc(n);
    if (p == NULL || nkept == sizeof kept / sizeof kept[0]) {
        host_log("could not keep a program's data: it cannot be restarted");
        free(p);
        return;
    }
    memcpy(p, s->data, n);
    kept[nkept].span = s;
    kept[nkept].data = p;
    nkept++;
}

void host_keep_start(void) {
    keep(&host_span);
}

void host_keep_program(const HOST_PROGRAM* program) {
    keep(&program->span);
}

/* a block that outlives the restarts, so that the same program's next start finds its memory whole: the one held if
   it is that size, else a new one */
void* host_keep_block(size_t size) {
    static void* block;
    static size_t held;
    if (block == NULL || held != size) {
        free(block);
        block = malloc(size);
        held = block != NULL ? size : 0;
    }
    return block;
}

void (*host_restart_hook)(int after);

void host_restart(void) {
    int i;
    board_stop();
    if (host_restart_hook != NULL) {
        host_restart_hook(0);
    }
    for (i = 0; i < nkept; i++) {
        const HOST_SPAN* s = kept[i].span;
        memcpy(s->data, kept[i].data, (size_t)(s->data_end - s->data));
        memset(s->bss, 0, (size_t)(s->bss_end - s->bss));
    }
    if (host_restart_hook != NULL) {
        host_restart_hook(1);
    }
}
