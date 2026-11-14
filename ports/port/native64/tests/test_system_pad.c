/* Run the reconstructed arcade pad reader with actual pdGetPeripheral and
   nlArcTan2 implementations. Peripheral records are configured as input
   fixtures; the test does not establish physical-controller compatibility. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../../src/system.c"

int main(void)
{
    PDS_PERIPHERALINFO info[2] = {{0}};
    PDS_PERIPHERAL *first = pdGetPeripheral(6);
    PDS_PERIPHERAL *second = pdGetPeripheral(12);
    assert(first != second);
    const int high_pointer = (uintptr_t)sw_work > UINT32_MAX;
    memset(sw_work, 0, sizeof(sw_work));
    memset(first, 0, sizeof(*first));
    memset(second, 0, sizeof(*second));
    info[0].type = info[1].type = PDD_DEVTYPE_CONTROLLER;
    first->info = &info[0];
    second->info = &info[1];
    first->on = PDD_DGT_TX | PDD_DGT_ST | PDD_DGT_KL;
    first->l = 17;
    first->r = 29;
    second->on = PDD_DGT_TB;
    second->x1 = 127;
    second->y1 = 0;
    second->l = 37;
    second->r = 43;
    sw_work[2].now = 0x55aa;
    conf_vms_mode = 2;
    rom_dma_flag = 0;

    pad_read(1);
    assert(sw_work[1].now == (KEY_HK | KEY_RIGHT));
    assert(sw_work[1].stick_x == 127 && sw_work[1].stick_y == 0);
    assert(sw_work[1].trg_l == 37 && sw_work[1].trg_r == 43);
    assert(sw_work[0].now == 0 && sw_work[2].now == 0x55aa);
    pad_read(0);
    assert(sw_work[0].now == (KEY_LP | KEY_START | KEY_LEFT));
    assert(sw_work[0].trg_l == 17 && sw_work[0].trg_r == 29);
    assert(sw_work[1].now == (KEY_HK | KEY_RIGHT));

    /* Existing cabinet directions take priority; its held assist survives. */
    sw_work[0].now = KEY_UP | KEY_A1;
    pad_read(0);
    assert(sw_work[0].now == (KEY_UP | KEY_A1 | KEY_LP | KEY_START));

    SW_WORK previous[4];
    memcpy(previous, sw_work, sizeof(previous));
    first->on = second->on = 0;
    rom_dma_flag = 1;
    pad_read(0);
    pad_read(1);
    assert(memcmp(previous, sw_work, sizeof(previous)) == 0);
    rom_dma_flag = 0;
    conf_vms_mode = 0;
    pad_read(0);
    assert(memcmp(previous, sw_work, sizeof(previous)) == 0);
    conf_vms_mode = 2;
    info[0].type = 0;
    pad_read(0);
    assert(memcmp(previous, sw_work, sizeof(previous)) == 0);
    printf("Native arcade pad reader: side indexing, real angle mapping and input gates passed (%s storage).\n",
           high_pointer ? "above 4 GiB" : "below 4 GiB");
    return 0;
}
