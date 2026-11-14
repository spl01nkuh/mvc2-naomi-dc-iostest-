/* Exercise the real private queue admission/reset logic, including addresses
   that a Uint32 cast would truncate. SDK-dependent DMA/sound execution paths
   are excluded by dead stripping, not implemented by test stubs. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../../../src/am_load.c"

int main(void)
{
    Uint8 *buffer = malloc(8192);
    assert(buffer != NULL);
    const int high_pointer = (uintptr_t)buffer > UINT32_MAX;
    _Static_assert(sizeof(dma_req_buf[0].src) == 4, "cartridge offsets stay serialized words");
    _Static_assert(sizeof(snd_req_buf[0].src) == sizeof(void *), "sound sources stay native pointers");
    _Static_assert(offsetof(LOAD_CTRL, speed) == 8, "reset clears the original eight control bytes");

    load_speed_set(0);
    assert(ld_ctrl.speed == 0x100);
    load_speed_set(0x321);
    load_init();
    assert(ld_ctrl.speed == 0x321 && load_ck() == 0);
    load_start();
    assert(load_ck() == 1 && load_total == 0 && load_log[0] == 0);

    DMA_REQ *request = dma_req(0, UINT32_C(0x88012340), buffer + 3, 4096);
    assert(request == &dma_req_buf[0]);
    assert(request->src == UINT32_C(0x08012340) && request->src0 == request->src);
    assert(request->dst == buffer + 3 && request->dst0 == buffer + 3);
    assert(request->size == 4096 && request->flag == 1);
    assert(ld_ctrl.dma_flag == 1 && ld_ctrl.dma_put == 1);
    snd_req(1, request->dst0, 3, request->size);
    assert(snd_req_buf[0].src == buffer + 3);
    assert(snd_req_buf[0].dst == 3 && snd_req_buf[0].size == 4096);
    assert(snd_req_buf[0].step == 0 && snd_req_buf[0].kind == 1);
    assert(ld_ctrl.snd_flag == 1 && ld_ctrl.snd_put == 1);
    snd_req(0, NULL, 7, 0);
    assert(snd_req_buf[1].src == NULL && snd_req_buf[1].dst == 7);

    /* The original queue is a 128-slot ring. Check indexing and native source
       preservation through wrap, without claiming overflow detection that the
       original queue does not supply. */
    load_init();
    for (unsigned index = 0; index < 128; ++index) {
        request = dma_req(1, UINT32_C(0x01230000) + index, buffer + index, 16);
        assert(request == &dma_req_buf[index]);
        assert(request->dst == buffer + index && request->src0 == UINT32_C(0x01230000) + index);
        snd_req(1, buffer + index, index % 6 + 2, 16);
        assert(snd_req_buf[index].src == buffer + index);
    }
    assert(ld_ctrl.dma_put == 0 && ld_ctrl.snd_put == 0);
    request = dma_req(0, 8, buffer + 1024, 32);
    assert(request == &dma_req_buf[0] && request->dst == buffer + 1024);
    load_init();
    assert(ld_ctrl.dma_flag == 0 && ld_ctrl.snd_flag == 0);
    for (unsigned index = 0; index < 128; ++index) {
        assert(dma_req_buf[index].flag == 0 && snd_req_buf[index].flag == 0);
    }
    free(buffer);
    printf("Native queue admission/reset: ROM offsets, native pointers and ring indexing passed (%s allocation).\n",
           high_pointer ? "above 4 GiB" : "below 4 GiB");
    return 0;
}
