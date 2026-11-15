/* The game's backup descriptor crosses a native C ABI boundary. Exercise the
 * actual VMU image library so its pointer fields cannot silently drift from
 * vms.c; the serialized header stays in its original byte layout. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game_memory.h"
#include "../../src/vms.c"

_Static_assert(sizeof(void *) == 8, "this regression requires native 64-bit pointers");
_Static_assert(sizeof(((BU_FILE_HEAD *)0)->visual_data) == sizeof(void *),
               "the backup descriptor must retain its full visual pointer");

static void pattern(Uint8 *bytes, size_t size, unsigned seed) {
    for (size_t i = 0; i < size; ++i)
        bytes[i] = (Uint8)(seed + i * 37 + i / 11);
}

static void test_system_file(void) {
    Uint8 expected[0x728];
    pattern(expected, sizeof expected, 19);
    memcpy(&vms_sys_buf, expected, sizeof expected);

    assert(vms_file_make(1) == (0x280 + 0x728 + 511) / 512);
    assert(vms_buf_a.size == sizeof expected);
    BU_FILE_HEAD description;
    assert(buAnalyzeBackupFileImage(&description, &vms_buf_a) == 0);
    Uint8 *image = (Uint8 *)&vms_buf_a;
    assert(description.icon == image + 0x60);
    assert(description.icon_data == image + 0x80);
    assert(description.icon_cnt == 1 && description.icon_spd == 5);
    assert(description.visual_data == NULL && description.visual_type == 0);
    assert(description.data == image + 0x280);
    assert(description.size == sizeof expected);
    assert(memcmp(description.icon, vms_icon_pal_tbl[1], 32) == 0);
    assert(memcmp(description.icon_data, vms_icon_img_tbl[1], 512) == 0);
    assert(memcmp(description.data, expected, sizeof expected) == 0);

    memset(&vms_sys_buf, 0, sizeof expected);
    assert(vms_file_unpack(1) == 0);
    assert(memcmp(&vms_sys_buf, expected, sizeof expected) == 0);

    image[0x280 + 17] ^= 1;
    memset(&vms_sys_buf, 0x5a, sizeof expected);
    assert(vms_file_unpack(1) == -224); /* the real library's CRC error */
    for (size_t i = 0; i < sizeof expected; ++i)
        assert(((Uint8 *)&vms_sys_buf)[i] == 0x5a);
    image[0x280 + 17] ^= 1;
    assert(vms_file_unpack(1) == 0);
    assert(memcmp(&vms_sys_buf, expected, sizeof expected) == 0);
}

static void test_upload_file(void) {
    _Static_assert(sizeof VMS_UP_BUF == 0x6000,
                   "the upload needs its complete native byte buffer");
    Uint8 expected[0x6000];
    Uint8 system[sizeof vms_sys_buf], download[sizeof vms_dow_buf], network[sizeof vms_net_buf];
    pattern(expected, sizeof expected, 31);
    pattern(system, sizeof system, 41);
    pattern(download, sizeof download, 43);
    pattern(network, sizeof network, 47);
    memcpy(&vms_sys_buf, system, sizeof system);
    memcpy(&vms_dow_buf, download, sizeof download);
    memcpy(&vms_net_buf, network, sizeof network);
    memcpy(VMS_UP_BUF, expected, sizeof expected);

    assert(vms_file_make(0) == sizeof vms_buf_a / 512);
    assert(vms_buf_a.size == sizeof expected);
    BU_FILE_HEAD description;
    assert(buAnalyzeBackupFileImage(&description, &vms_buf_a) == 0);
    assert(description.data == (Uint8 *)&vms_buf_a + 0x280);
    assert(description.size == sizeof expected);
    assert(memcmp(description.data, expected, sizeof expected) == 0);
    assert(memcmp(&vms_sys_buf, system, sizeof system) == 0);
    assert(memcmp(&vms_dow_buf, download, sizeof download) == 0);
    assert(memcmp(&vms_net_buf, network, sizeof network) == 0);

    memset(VMS_UP_BUF, 0, sizeof expected);
    assert(vms_file_unpack(0) == 0);
    assert(memcmp(VMS_UP_BUF, expected, sizeof expected) == 0);
    assert(memcmp(&vms_sys_buf, system, sizeof system) == 0);
    assert(memcmp(&vms_dow_buf, download, sizeof download) == 0);
    assert(memcmp(&vms_net_buf, network, sizeof network) == 0);
}

static void test_visual_descriptors(void) {
    const size_t visual_sizes[] = {0, 72 * 56 * 2, 512 + 72 * 56, 32 + 72 * 56 / 2};
    Uint8 palette[32], icons[3 * 512], visual[72 * 56 * 2], payload[73];
    Uint8 image[0x4000];
    pattern(palette, sizeof palette, 3);
    pattern(icons, sizeof icons, 7);
    pattern(visual, sizeof visual, 11);
    pattern(payload, sizeof payload, 23);
    /* These addresses must exercise the bits the old Sint32 field lost. */
    assert((uintptr_t)visual > UINT32_MAX);
    assert((uintptr_t)payload > UINT32_MAX);

    for (unsigned type = 1; type <= 3; ++type) {
        BU_FILE_HEAD source = {0};
        memcpy(source.vms_name, "MVC2 pointer test", 16);
        memcpy(source.comment, "native visual and save data", 27);
        pattern(source.pal, sizeof source.pal, 29);
        source.icon = palette;
        source.icon_data = icons;
        source.icon_cnt = (Uint16)type;
        source.icon_spd = 9;
        source.visual_data = visual;
        source.visual_type = (Uint16)type;
        source.data = payload;
        source.size = sizeof payload;
        size_t visual_at = 0x80 + type * 512;
        size_t data_at = visual_at + visual_sizes[type];
        assert(buMakeBackupFileImage(image, &source) == (int)((data_at + sizeof payload + 511) / 512));

        struct {
            BU_FILE_HEAD head;
            Uint8 canary[16];
        } decoded;
        memset(&decoded, 0xa5, sizeof decoded);
        assert(buAnalyzeBackupFileImage(&decoded.head, image) == 0);
        for (size_t i = 0; i < sizeof decoded.canary; ++i)
            assert(decoded.canary[i] == 0xa5);
        assert(decoded.head.icon == image + 0x60);
        assert(decoded.head.icon_data == image + 0x80);
        assert(decoded.head.icon_cnt == type && decoded.head.icon_spd == 9);
        assert(decoded.head.visual_type == type);
        assert(decoded.head.visual_data == image + visual_at);
        assert(decoded.head.data == image + data_at);
        assert(decoded.head.size == sizeof payload);
        assert(memcmp(decoded.head.visual_data, visual, visual_sizes[type]) == 0);
        assert(memcmp(decoded.head.data, payload, sizeof payload) == 0);

        image[data_at + sizeof payload - 1] ^= 1;
        assert(buAnalyzeBackupFileImage(&decoded.head, image) == -224);
    }
}

int main(void) {
    mvc2_game_memory_reset();
#ifdef TARGET_DC
    assert(VMS_EXEC_IMAGE == host_native64_ram_pointer(0x0ce1e000, 1));
#else
    assert(VMS_EXEC_IMAGE == host_native64_ram_pointer(0x0d980000, 1));
#endif
    ((Uint8 *)VMS_EXEC_IMAGE)[0] = 0x63;
    assert(((Uint8 *)VMS_EXEC_IMAGE)[0] == 0x63);
    test_system_file();
    test_upload_file();
    test_visual_descriptors();
    puts("Native VMU backup: system/upload round trips, independent buffers, full visual/data pointers and CRC rejection passed.");
    return 0;
}
