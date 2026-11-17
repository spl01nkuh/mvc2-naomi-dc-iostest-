/* All published character local records must fit the native scratch array.
 * Run the real Captain America and SonSon initialization routines, and Rogue's
 * shared shield-state reader, with pointers above the serialized word range. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player.h"
#define COLOR_STEP PL00_COLOR_STEP
#include "pl00_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL01_COLOR_STEP
#include "pl01_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL02_COLOR_STEP
#include "pl02_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL03_COLOR_STEP
#include "pl03_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL05_COLOR_STEP
#include "pl05_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL06_COLOR_STEP
#include "pl06_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL07_COLOR_STEP
#include "pl07_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL08_COLOR_STEP
#include "pl08_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL09_COLOR_STEP
#include "pl09_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL0A_COLOR_STEP
#include "pl0a_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL0B_COLOR_STEP
#include "pl0b_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL0C_COLOR_STEP
#include "pl0c_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL0D_COLOR_STEP
#include "pl0d_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL0E_COLOR_STEP
#include "pl0e_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL0F_COLOR_STEP
#include "pl0f_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL10_COLOR_STEP
#include "pl10_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL11_COLOR_STEP
#include "pl11_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL12_COLOR_STEP
#include "pl12_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL13_COLOR_STEP
#include "pl13_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL14_COLOR_STEP
#include "pl14_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL15_COLOR_STEP
#include "pl15_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL16_COLOR_STEP
#include "pl16_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL17_COLOR_STEP
#include "pl17_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL18_COLOR_STEP
#include "pl18_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL19_COLOR_STEP
#include "pl19_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL1A_COLOR_STEP
#include "pl1a_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL1B_COLOR_STEP
#include "pl1b_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL1C_COLOR_STEP
#include "pl1c_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL1D_COLOR_STEP
#include "pl1d_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL1E_COLOR_STEP
#include "pl1e_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL1F_COLOR_STEP
#include "pl1f_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL20_COLOR_STEP
#include "pl20_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL21_COLOR_STEP
#include "pl21_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL22_COLOR_STEP
#include "pl22_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL24_COLOR_STEP
#include "pl24_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL25_COLOR_STEP
#include "pl25_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL26_COLOR_STEP
#include "pl26_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL27_COLOR_STEP
#include "pl27_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL2A_COLOR_STEP
#include "pl2a_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL2B_COLOR_STEP
#include "pl2b_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL2C_COLOR_STEP
#include "pl2c_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL2D_COLOR_STEP
#include "pl2d_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL2E_COLOR_STEP
#include "pl2e_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL2F_COLOR_STEP
#include "pl2f_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL30_COLOR_STEP
#include "pl30_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL31_COLOR_STEP
#include "pl31_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL32_COLOR_STEP
#include "pl32_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL33_COLOR_STEP
#include "pl33_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL34_COLOR_STEP
#include "pl34_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL35_COLOR_STEP
#include "pl35_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL36_COLOR_STEP
#include "pl36_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL37_COLOR_STEP
#include "pl37_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL38_COLOR_STEP
#include "pl38_def.h"
#undef COLOR_STEP
#define COLOR_STEP PL3A_COLOR_STEP
#include "pl3a_def.h"
#undef COLOR_STEP

#define LOCAL_FITS( type ) \
    _Static_assert( sizeof( type ) <= sizeof( ((PLAYER_WORK *)0)->tlocal ), #type " exceeds native scratch" ); \
    _Static_assert( offsetof( PLAYER_WORK, tlocal ) % _Alignof( type ) == 0, #type " is not aligned" )
LOCAL_FITS( PL00_LOCAL_W );
LOCAL_FITS( PL01_LOCAL_W );
LOCAL_FITS( PL02_LOCAL_W );
LOCAL_FITS( PL03_LOCAL_W );
LOCAL_FITS( PL05_LOCAL_W );
LOCAL_FITS( PL06_LOCAL_W );
LOCAL_FITS( PL07_LOCAL_W );
LOCAL_FITS( PL08_LOCAL_W );
LOCAL_FITS( PL09_LOCAL_W );
LOCAL_FITS( PL0A_LOCAL_W );
LOCAL_FITS( PL0B_LOCAL_W );
LOCAL_FITS( PL0C_LOCAL_W );
LOCAL_FITS( PL0D_LOCAL_W );
LOCAL_FITS( PL0E_LOCAL_W );
LOCAL_FITS( PL0F_LOCAL_W );
LOCAL_FITS( PL10_LOCAL_W );
LOCAL_FITS( PL11_LOCAL_W );
LOCAL_FITS( PL12_LOCAL_W );
LOCAL_FITS( PL13_LOCAL_W );
LOCAL_FITS( PL14_LOCAL_W );
LOCAL_FITS( PL15_LOCAL_W );
LOCAL_FITS( PL16_LOCAL_W );
LOCAL_FITS( PL17_LOCAL_W );
LOCAL_FITS( PL18_LOCAL_W );
LOCAL_FITS( PL19_LOCAL_W );
LOCAL_FITS( PL1A_LOCAL_W );
LOCAL_FITS( PL1B_LOCAL_W );
LOCAL_FITS( PL1C_LOCAL_W );
LOCAL_FITS( PL1D_LOCAL_W );
LOCAL_FITS( PL1E_LOCAL_W );
LOCAL_FITS( PL1F_LOCAL_W );
LOCAL_FITS( PL20_LOCAL_W );
LOCAL_FITS( PL21_LOCAL_W );
LOCAL_FITS( PL22_LOCAL_W );
LOCAL_FITS( PL24_LOCAL_W );
LOCAL_FITS( PL25_LOCAL_W );
LOCAL_FITS( PL26_LOCAL_W );
LOCAL_FITS( PL27_LOCAL_W );
LOCAL_FITS( PL2A_LOCAL_W );
LOCAL_FITS( PL2B_LOCAL_W );
LOCAL_FITS( PL2C_LOCAL_W );
LOCAL_FITS( PL2D_LOCAL_W );
LOCAL_FITS( PL2E_LOCAL_W );
LOCAL_FITS( PL2F_LOCAL_W );
LOCAL_FITS( PL30_LOCAL_W );
LOCAL_FITS( PL31_LOCAL_W );
LOCAL_FITS( PL32_LOCAL_W );
LOCAL_FITS( PL33_LOCAL_W );
LOCAL_FITS( PL34_LOCAL_W );
LOCAL_FITS( PL35_LOCAL_W );
LOCAL_FITS( PL36_LOCAL_W );
LOCAL_FITS( PL37_LOCAL_W );
LOCAL_FITS( PL38_LOCAL_W );
LOCAL_FITS( PL3A_LOCAL_W );

/* Rogue has small views of several other characters. Rename those test-local
   declarations to permit the actual character headers above in this unit. */
#define PL12_LOCAL_W ROGUE_PL12_LOCAL_W
#define PL29_LOCAL_W ROGUE_PL29_LOCAL_W
#define PL2D_LOCAL_W ROGUE_PL2D_LOCAL_W
#define PL2F_LOCAL_W ROGUE_PL2F_LOCAL_W
#define PL31_LOCAL_W ROGUE_PL31_LOCAL_W
#define PL37_LOCAL_W ROGUE_PL37_LOCAL_W
/* The original timer API takes a halfword pointer while Rogue's numeric
   counters are words; that unrelated historical diagnostic is kept local. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#include "../../src/s_pl0a.c"
#pragma GCC diagnostic pop
#undef PL12_LOCAL_W
#undef PL29_LOCAL_W
#undef PL2D_LOCAL_W
#undef PL2F_LOCAL_W
#undef PL31_LOCAL_W
#undef PL37_LOCAL_W

extern void pl0b_tlocal_init( PLAYER_WORK *player );
extern void pl14_tlocal_init( PLAYER_WORK *player );

static void assert_outside_unchanged( const PLAYER_WORK *player, const unsigned char *saved ) {
    size_t start = offsetof( PLAYER_WORK, tlocal );
    size_t end = start + sizeof player->tlocal;
    assert( memcmp( player, saved, start ) == 0 );
    assert( memcmp( (const unsigned char *)player + end, saved + end, sizeof *player - end ) == 0 );
}
static void test_shield_local( void ) {
    PLAYER_WORK player, shield;
    unsigned char saved[sizeof player], expected[sizeof player.tlocal];
    memset( &player, 0xa5, sizeof player );
    PL0B_LOCAL_W *local = PL0B_LW( &player );
    assert( (uintptr_t)local % _Alignof( PL0B_LOCAL_W ) == 0 );
    assert( (uintptr_t)&shield > UINT32_MAX );
    local->shield_w = &shield;
    local->shield_st = 3; local->shield_sub = 5; local->shield_type = 7;
    memcpy( saved, &player, sizeof saved );
    pl0b_tlocal_init( &player );
    memset( expected, 0, sizeof expected );
    PL0B_LOCAL_W result = {0};
    result.shield_w = &shield; result.shield_st = 3; result.shield_sub = 5; result.shield_type = 7;
    memcpy( expected, &result, sizeof result );
    assert( memcmp( player.tlocal, expected, sizeof expected ) == 0 );
    assert_outside_unchanged( &player, saved );
    /* The old Rogue view read byte 0x0e, inside em_w's native pointer. Give it
       the opposite value to prove the actual typed shield field is read. */
    ((unsigned char *)player.tlocal)[0x0e] = 0;
    local->shield_st = 3;
    assert( !pl0a_shield_in_hand( &player ) );
    ((unsigned char *)player.tlocal)[0x0e] = 0xa5;
    local->shield_st = 0;
    assert( pl0a_shield_in_hand( &player ) );
}
static void test_sonson_local( void ) {
    PLAYER_WORK player, parts[13];
    unsigned char saved[sizeof player], expected[sizeof player.tlocal];
    memset( &player, 0xa5, sizeof player );
    PL14_LOCAL_W *local = (PL14_LOCAL_W *)(void *)player.tlocal;
    assert( (uintptr_t)local % _Alignof( PL14_LOCAL_W ) == 0 );
    local->shot_cnt = 11;
    for( int i = 0; i < 13; ++i ) local->part_w[i] = &parts[i];
    /* Writing the thirteenth native pointer formerly crossed into these. */
    assert( player.wall_wide_timer == 0xa5 && player.prio_ofs == 0xa5 );
    memcpy( saved, &player, sizeof saved );
    pl14_tlocal_init( &player );
    memset( expected, 0, sizeof expected );
    PL14_LOCAL_W result = {0}; result.shot_cnt = 11;
    memcpy( expected, &result, sizeof result );
    assert( memcmp( player.tlocal, expected, sizeof expected ) == 0 );
    assert_outside_unchanged( &player, saved );
}
int main( void ) {
    _Static_assert( sizeof ((PLAYER_WORK *)0)->tlocal[0] == 4, "indexed words remain serialized width" );
    test_shield_local(); test_sonson_local();
    puts( "Native character locals: all published layouts fit and align; real shield/SonSon resets and Rogue's shared shield view passed." );
    return 0;
}
