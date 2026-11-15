/* ctr_game_zones.c  Profile builds: the game's calls into the library's matrices, maths, board and ROM reads
   timed by layer, through the linker's --wrap (the names are in CMakeLists). All but the ROM reads come hundreds
   of times a frame: one call in 64 is timed, and counted 64 times */

#include "player.h"
#include "naomi.h"
#include "prof.h"

#define EVERY 64

static uint32_t sampled_n;

static void sampled_out(int zone, uint32_t t) {
    uint32_t d = prof_now() - t;
    prof_add(zone, (d > prof_read_ticks ? d - prof_read_ticks : 0u) * EVERY);
    prof_count(zone, EVERY);
}

#define TIMED_IN(zone) uint32_t t_ = prof_now()
#define TIMED_OUT(zone) prof_add((zone), prof_now() - t_)
#define SAMPLED_IN(zone) uint32_t t_ = (++sampled_n & (EVERY - 1)) == 0 ? prof_now() : 0u
#define SAMPLED_OUT(zone) ((void)(t_ != 0u ? sampled_out((zone), t_), 0 : 0))

Sint32 __real_nlArcTan2(Float y, Float x);
Sint32 __wrap_nlArcTan2(Float y, Float x) {
    SAMPLED_IN(PZ_GAME_MATH);
    Sint32 r = __real_nlArcTan2(y, x);
    SAMPLED_OUT(PZ_GAME_MATH);
    return r;
}

void __real_nlCalcPoint(VEC3 *src, VEC3 *dst);
void __wrap_nlCalcPoint(VEC3 *src, VEC3 *dst) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlCalcPoint(src, dst);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlCalcVector(VEC3 *src, VEC3 *dst);
void __wrap_nlCalcVector(VEC3 *src, VEC3 *dst) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlCalcVector(src, dst);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

Float __real_nlFloor(Float n);
Float __wrap_nlFloor(Float n) {
    SAMPLED_IN(PZ_GAME_MATH);
    Float r = __real_nlFloor(n);
    SAMPLED_OUT(PZ_GAME_MATH);
    return r;
}

void __real_nlFogDensity(Sint32 density);
void __wrap_nlFogDensity(Sint32 density) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlFogDensity(density);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

int __real_nlFogDensityF(Float fdensity);
int __wrap_nlFogDensityF(Float fdensity) {
    SAMPLED_IN(PZ_GAME_BOARD);
    int r = __real_nlFogDensityF(fdensity);
    SAMPLED_OUT(PZ_GAME_BOARD);
    return r;
}

void __real_nlFogTableColor(Uint32 argb);
void __wrap_nlFogTableColor(Uint32 argb) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlFogTableColor(argb);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlFogTableOff(void);
void __wrap_nlFogTableOff(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlFogTableOff();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlFogTableSet(Float *table);
void __wrap_nlFogTableSet(Float *table) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlFogTableSet(table);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

MATRIX * __real_nlGetMatrix(MATRIX *md);
MATRIX * __wrap_nlGetMatrix(MATRIX *md) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    MATRIX * r = __real_nlGetMatrix(md);
    SAMPLED_OUT(PZ_GAME_MATRIX);
    return r;
}

void __real_nlInitLibrary(int tvmode, int fbmode, int vwait);
void __wrap_nlInitLibrary(int tvmode, int fbmode, int vwait) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlInitLibrary(tvmode, fbmode, vwait);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

MATRIX * __real_nlInitMatrix(MATRIX *m, Sint32 n);
MATRIX * __wrap_nlInitMatrix(MATRIX *m, Sint32 n) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    MATRIX * r = __real_nlInitMatrix(m, n);
    SAMPLED_OUT(PZ_GAME_MATRIX);
    return r;
}

void __real_nlLoadMatrix(MATRIX *ms);
void __wrap_nlLoadMatrix(MATRIX *ms) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlLoadMatrix(ms);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlLookAt(VEC3 *eye, VEC3 *obj, Sint32 z);
void __wrap_nlLookAt(VEC3 *eye, VEC3 *obj, Sint32 z) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlLookAt(eye, obj, z);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlMakeModelViewProjectMatrix(Float width, Float height);
void __wrap_nlMakeModelViewProjectMatrix(Float width, Float height) {
    SAMPLED_IN(PZ_GAME_MATH);
    __real_nlMakeModelViewProjectMatrix(width, height);
    SAMPLED_OUT(PZ_GAME_MATH);
}

void __real_nlMatrixMode(int mode);
void __wrap_nlMatrixMode(int mode) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlMatrixMode(mode);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlMultiMatrix(MATRIX *ms);
void __wrap_nlMultiMatrix(MATRIX *ms) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlMultiMatrix(ms);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlObjPutSetFadeColor(Float r, Float g, Float b);
void __wrap_nlObjPutSetFadeColor(Float r, Float g, Float b) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlObjPutSetFadeColor(r, g, b);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlPerspective(Sint32 fovy, Float aspect, Float znear, Float zfar);
void __wrap_nlPerspective(Sint32 fovy, Float aspect, Float znear, Float zfar) {
    SAMPLED_IN(PZ_GAME_MATH);
    __real_nlPerspective(fovy, aspect, znear, zfar);
    SAMPLED_OUT(PZ_GAME_MATH);
}

void __real_nlPerspectiveX(Sint32 fovx, Float aspect, Float znear, Float zfar);
void __wrap_nlPerspectiveX(Sint32 fovx, Float aspect, Float znear, Float zfar) {
    SAMPLED_IN(PZ_GAME_MATH);
    __real_nlPerspectiveX(fovx, aspect, znear, zfar);
    SAMPLED_OUT(PZ_GAME_MATH);
}

int __real_nlPopMatrix(int n);
int __wrap_nlPopMatrix(int n) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    int r = __real_nlPopMatrix(n);
    SAMPLED_OUT(PZ_GAME_MATRIX);
    return r;
}

Float __real_nlPow(Float x, Float y);
Float __wrap_nlPow(Float x, Float y) {
    SAMPLED_IN(PZ_GAME_MATH);
    Float r = __real_nlPow(x, y);
    SAMPLED_OUT(PZ_GAME_MATH);
    return r;
}

void __real_nlProjectScreen(VEC3 *pos, Float *scr);
void __wrap_nlProjectScreen(VEC3 *pos, Float *scr) {
    SAMPLED_IN(PZ_GAME_MATH);
    __real_nlProjectScreen(pos, scr);
    SAMPLED_OUT(PZ_GAME_MATH);
}

int __real_nlPushMatrix(MATRIX *m);
int __wrap_nlPushMatrix(MATRIX *m) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    int r = __real_nlPushMatrix(m);
    SAMPLED_OUT(PZ_GAME_MATRIX);
    return r;
}

Sint32 __real_nlRand(void);
Sint32 __wrap_nlRand(void) {
    SAMPLED_IN(PZ_GAME_MATH);
    Sint32 r = __real_nlRand();
    SAMPLED_OUT(PZ_GAME_MATH);
    return r;
}

Float __real_nlRandom(void);
Float __wrap_nlRandom(void) {
    SAMPLED_IN(PZ_GAME_MATH);
    Float r = __real_nlRandom();
    SAMPLED_OUT(PZ_GAME_MATH);
    return r;
}

void __real_nlRotateAX(Sint32 ang);
void __wrap_nlRotateAX(Sint32 ang) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlRotateAX(ang);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlRotateAY(Sint32 ang);
void __wrap_nlRotateAY(Sint32 ang) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlRotateAY(ang);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlRotateAZ(Sint32 ang);
void __wrap_nlRotateAZ(Sint32 ang) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlRotateAZ(ang);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlScaleV(VEC3 *v);
void __wrap_nlScaleV(VEC3 *v) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlScaleV(v);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlScreenPoint(Float x, Float y, Float z, Float *dx, Float *dy, Float *dz);
void __wrap_nlScreenPoint(Float x, Float y, Float z, Float *dx, Float *dy, Float *dz) {
    SAMPLED_IN(PZ_GAME_MATH);
    __real_nlScreenPoint(x, y, z, dx, dy, dz);
    SAMPLED_OUT(PZ_GAME_MATH);
}

void __real_nlSetBackColor(Uint32 col1, Uint32 col2, Uint32 col3);
void __wrap_nlSetBackColor(Uint32 col1, Uint32 col2, Uint32 col3) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlSetBackColor(col1, col2, col3);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlSetBackColorZ(Uint32 col1, Uint32 col2, Uint32 col3, Float z);
void __wrap_nlSetBackColorZ(Uint32 col1, Uint32 col2, Uint32 col3, Float z) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlSetBackColorZ(col1, col2, col3, z);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlSetScaleFactor(Float s);
void __wrap_nlSetScaleFactor(Float s) {
    SAMPLED_IN(PZ_GAME_MATH);
    __real_nlSetScaleFactor(s);
    SAMPLED_OUT(PZ_GAME_MATH);
}

void __real_nlShiftPerspective(Float x, Float y);
void __wrap_nlShiftPerspective(Float x, Float y) {
    SAMPLED_IN(PZ_GAME_MATH);
    __real_nlShiftPerspective(x, y);
    SAMPLED_OUT(PZ_GAME_MATH);
}

void __real_nlTranslate(Float x, Float y, Float z);
void __wrap_nlTranslate(Float x, Float y, Float z) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlTranslate(x, y, z);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlTranslateV(VEC3 *v);
void __wrap_nlTranslateV(VEC3 *v) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlTranslateV(v);
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

void __real_nlUnitMatrix(void);
void __wrap_nlUnitMatrix(void) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    __real_nlUnitMatrix();
    SAMPLED_OUT(PZ_GAME_MATRIX);
}

Float __real_nlUnitVector(VEC3 *v);
Float __wrap_nlUnitVector(VEC3 *v) {
    SAMPLED_IN(PZ_GAME_MATRIX);
    Float r = __real_nlUnitVector(v);
    SAMPLED_OUT(PZ_GAME_MATRIX);
    return r;
}

void __real_nlaChangeCreditSequence(int pl, int sequence);
void __wrap_nlaChangeCreditSequence(int pl, int sequence) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaChangeCreditSequence(pl, sequence);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

int __real_nlaCheckCreditEnough(int pl, int sequence);
int __wrap_nlaCheckCreditEnough(int pl, int sequence) {
    SAMPLED_IN(PZ_GAME_BOARD);
    int r = __real_nlaCheckCreditEnough(pl, sequence);
    SAMPLED_OUT(PZ_GAME_BOARD);
    return r;
}

void __real_nlaGetBackupData(NLA_BACKUP_BANK *buffer);
void __wrap_nlaGetBackupData(NLA_BACKUP_BANK *buffer) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaGetBackupData(buffer);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaGetBootFlag(void);
void __wrap_nlaGetBootFlag(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaGetBootFlag();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

NLA_CREDIT_CONF * __real_nlaGetCreditConf(void);
NLA_CREDIT_CONF * __wrap_nlaGetCreditConf(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    NLA_CREDIT_CONF * r = __real_nlaGetCreditConf();
    SAMPLED_OUT(PZ_GAME_BOARD);
    return r;
}

void __real_nlaGetCreditData(NLA_CREDIT_INFO *info);
void __wrap_nlaGetCreditData(NLA_CREDIT_INFO *info) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaGetCreditData(info);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaGetRTC(NLA_RTC_DATE *date);
void __wrap_nlaGetRTC(NLA_RTC_DATE *date) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaGetRTC(date);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

NLA_SYSTEM_FLAG * __real_nlaGetSystemFlag(void);
NLA_SYSTEM_FLAG * __wrap_nlaGetSystemFlag(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    NLA_SYSTEM_FLAG * r = __real_nlaGetSystemFlag();
    SAMPLED_OUT(PZ_GAME_BOARD);
    return r;
}

void __real_nlaInTestMode(void);
void __wrap_nlaInTestMode(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaInTestMode();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaInitSystem(void);
void __wrap_nlaInitSystem(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaInitSystem();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

int __real_nlaInitUserBackup(int size, char *sdram);
int __wrap_nlaInitUserBackup(int size, char *sdram) {
    SAMPLED_IN(PZ_GAME_BOARD);
    int r = __real_nlaInitUserBackup(size, sdram);
    SAMPLED_OUT(PZ_GAME_BOARD);
    return r;
}

int __real_nlaInitUserEEPROM(int size, char *sdram);
int __wrap_nlaInitUserEEPROM(int size, char *sdram) {
    SAMPLED_IN(PZ_GAME_BOARD);
    int r = __real_nlaInitUserEEPROM(size, sdram);
    SAMPLED_OUT(PZ_GAME_BOARD);
    return r;
}

void __real_nlaJAMMACreditInit(void);
void __wrap_nlaJAMMACreditInit(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaJAMMACreditInit();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaJAMMAInit(void);
void __wrap_nlaJAMMAInit(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaJAMMAInit();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaJAMMASetPrimaryNode(int node);
void __wrap_nlaJAMMASetPrimaryNode(int node) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaJAMMASetPrimaryNode(node);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaJumpTestMode(void);
void __wrap_nlaJumpTestMode(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaJumpTestMode();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaNRSDecodeRead(unsigned long src, char *dst, unsigned long size, int flag);
void __wrap_nlaNRSDecodeRead(unsigned long src, char *dst, unsigned long size, int flag) {
    TIMED_IN(PZ_GAME_ROM);
    __real_nlaNRSDecodeRead(src, dst, size, flag);
    TIMED_OUT(PZ_GAME_ROM);
}

void __real_nlaPutLED(unsigned char led);
void __wrap_nlaPutLED(unsigned char led) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaPutLED(led);
    SAMPLED_OUT(PZ_GAME_BOARD);
}

int __real_nlaROMTypeSet(int type, int a);
int __wrap_nlaROMTypeSet(int type, int a) {
    SAMPLED_IN(PZ_GAME_BOARD);
    int r = __real_nlaROMTypeSet(type, a);
    SAMPLED_OUT(PZ_GAME_BOARD);
    return r;
}

void __real_nlaReadROMDMA(unsigned long src, char *dst, unsigned long size, int flag);
void __wrap_nlaReadROMDMA(unsigned long src, char *dst, unsigned long size, int flag) {
    TIMED_IN(PZ_GAME_ROM);
    __real_nlaReadROMDMA(src, dst, size, flag);
    TIMED_OUT(PZ_GAME_ROM);
}

void __real_nlaUpdateSystemBackup(void);
void __wrap_nlaUpdateSystemBackup(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaUpdateSystemBackup();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaUpdateUserBackup(void);
void __wrap_nlaUpdateUserBackup(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaUpdateUserBackup();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

void __real_nlaUpdateUserEEPROM(void);
void __wrap_nlaUpdateUserEEPROM(void) {
    SAMPLED_IN(PZ_GAME_BOARD);
    __real_nlaUpdateUserEEPROM();
    SAMPLED_OUT(PZ_GAME_BOARD);
}

/* MVC2_XMTR_BENCH=n: at the start, the library's matrix routines timed on their own, n calls each, warm in the cache,
   those the 3DS replaces beside the plain ones. A turned matrix keeps the products in range; the game sets up its own
   stacks afterwards */
void host_log(const char* fmt, ...);
int atoi(const char* s);
char* getenv(const char* name);
void nl_xmtr_old_multi(MATRIX* m);
void nl_xmtr_old_translate(Float x, Float y, Float z);
void nl_xmtr_old_rotate_y(Sint32 ang);

static MATRIX bench_stack[9] __attribute__((aligned(32)));
static int bench_n;

static double bench_ticks(uint32_t t) {
    return (double)(t > prof_read_ticks ? t - prof_read_ticks : 0) / bench_n;
}

void ctr_xmtr_bench(void) {
    static const MATRIX turned = { { { 0.8f, 0.6f, 0.0f, 0.0f }, { -0.6f, 0.8f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f },
                                     { 1.0f, 2.0f, 3.0f, 1.0f } } };
    const char* e = getenv("MVC2_XMTR_BENCH");
    MATRIX m = turned, got;
    VEC3 v = { 1.5f, -2.25f, 3.0f }, w, one = { 1.0f, 1.0f, 1.0f }, eye = { 10.0f, 20.0f, 300.0f }, at = { 0.0f, 5.0f, 0.0f };
    int i;
    uint32_t t, t_old;
    if (e == NULL || (bench_n = atoi(e)) <= 0) {
        return;
    }
    __real_nlInitMatrix(bench_stack, 8);
    __real_nlLoadMatrix(&m);
#define TIMED(v, call)                                                                                                 \
    do {                                                                                                               \
        v = prof_now();                                                                                                \
        for (i = 0; i < bench_n; i++) {                                                                                \
            call;                                                                                                      \
        }                                                                                                              \
        v = prof_now() - v;                                                                                            \
    } while (0)
#define BENCH(name, call)                                                                                              \
    do {                                                                                                               \
        TIMED(t, call);                                                                                                \
        host_log("xmtr bench: %-28s %6.1f ticks a call", name, bench_ticks(t));                                        \
    } while (0)
#define BENCH2(name, call, old)                                                                                        \
    do {                                                                                                               \
        __real_nlLoadMatrix(&m);                                                                                       \
        TIMED(t, call);                                                                                                \
        __real_nlLoadMatrix(&m);                                                                                       \
        TIMED(t_old, old);                                                                                             \
        host_log("xmtr bench: %-28s %6.1f ticks a call, the plain one's %6.1f", name, bench_ticks(t),                  \
                 bench_ticks(t_old));                                                                                  \
    } while (0)
    BENCH("nlLoadMatrix", __real_nlLoadMatrix(&m));
    BENCH("nlGetMatrix", __real_nlGetMatrix(&got));
    BENCH("nlPushMatrix + nlPopMatrix", (__real_nlPushMatrix(NULL), __real_nlPopMatrix(1)));
    BENCH("nlUnitMatrix", __real_nlUnitMatrix());
    BENCH2("nlMultiMatrix", __real_nlMultiMatrix(&m), nl_xmtr_old_multi(&m));
    BENCH2("nlTranslate", __real_nlTranslate(0.5f, -0.25f, 0.125f), nl_xmtr_old_translate(0.5f, -0.25f, 0.125f));
    BENCH2("nlRotateAY, a new angle each", __real_nlRotateAY(i * 97), nl_xmtr_old_rotate_y(i * 97));
    BENCH2("nlRotateAY, one angle", __real_nlRotateAY(0x1234), nl_xmtr_old_rotate_y(0x1234));
    __real_nlLoadMatrix(&m);
    BENCH("nlScaleV", __real_nlScaleV(&one));
    BENCH("nlLookAt, turned", (__real_nlUnitMatrix(), __real_nlLookAt(&eye, &at, 0x800 + (i & 7))));
    BENCH("nlUnitVector", (w = v, __real_nlUnitVector(&w)));
    BENCH("nlCalcPoint", __real_nlCalcPoint(&v, &w));
#undef BENCH2
#undef BENCH
#undef TIMED
    __real_nlInitMatrix(bench_stack, 8);
}
