/* Synthetic PCM through the actual AICA register/mixer interface. Link the
 * real sound CPU/driver, naomi_board host_log and runtime host_keep_block;
 * function-section dead stripping removes unrelated game entry points. */
#include "aica.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

enum { REGISTERS = 0x00700000, PCM = 0x00801000, SAMPLES = 8 };

static void reg_write(unsigned channel, unsigned offset, uint32_t value) {
    aica_sh4_write32(REGISTERS + channel * 0x80 + offset, value);
}
static void select_channel(unsigned channel) {
    aica_sh4_write32(REGISTERS + 0x280c, channel << 8);
}
static void configure(unsigned channel) {
    reg_write(channel, 0x04, PCM - 0x00800000); /* signed 16-bit PCM */
    reg_write(channel, 0x08, 0);
    reg_write(channel, 0x0c, SAMPLES); /* nonlooping sample */
    reg_write(channel, 0x10, 31); /* immediate attack, held decay */
    reg_write(channel, 0x14, 15u << 10); /* no key-rate scaling */
    reg_write(channel, 0x18, 0x200); /* 1.5 source samples per output sample */
    reg_write(channel, 0x24, 15u << 8); /* full direct output, centre pan */
}
static void assert_released(unsigned channel) {
    select_channel(channel);
    uint32_t envelope = aica_sh4_read32(REGISTERS + 0x2810);
    assert((envelope >> 13 & 3) == 3); /* release, as read by the sound driver */
    assert((envelope >> 3 & 0x3ff) == 0x3ff); /* silent */
    assert(aica_sh4_read32(REGISTERS + 0x2814) == 0); /* address zero means free */
    assert(!(aica_sh4_read32(REGISTERS + channel * 0x80) & 0x4000)); /* KYONB clear */
}
int main(void) {
    assert(aica_init(1));
    aica_set_dsp(0); aica_set_filter(0); aica_set_half(0); aica_set_reverb(0);
    aica_sh4_write32(REGISTERS + 0x2800, 15);
    uint8_t* pcm = aica_sh4_ptr(PCM);
    assert(pcm);
    for (unsigned i = 0; i < SAMPLES; ++i) pcm[i * 2] = 0, pcm[i * 2 + 1] = 0x40;
    int16_t output[32 * 2];
    /* Use/reuse every hardware channel. A slot that has ended must be visible
     * as free through the registers before the driver will reuse it. */
    for (unsigned round = 0; round < 3; ++round) {
        for (unsigned channel = 0; channel < 64; ++channel) {
            configure(channel);
            reg_write(channel, 0, 0xc000); /* KYONB + execute key changes */
            aica_render(output, 1);
            select_channel(channel);
            assert(aica_sh4_read32(REGISTERS + 0x2814) == 1);
            assert((aica_sh4_read32(REGISTERS + 0x2810) >> 13 & 3) != 3);
            assert(output[0] > 0 && output[1] > 0);
            aica_render(output, 32); /* runs past the nonlooping sample's end */
            assert_released(channel);
            AICA_STATS status;
            aica_stats(&status);
            assert(status.active_channels == 0);
        }
    }
    aica_render(output, 32);
    for (unsigned i = 0; i < 32 * 2; ++i) assert(output[i] == 0);
    aica_free();
    puts("Native AICA: all PCM slots end silent/released at address zero and can be reused.");
    return 0;
}
