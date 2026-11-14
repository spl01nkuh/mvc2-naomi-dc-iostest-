/* Portable SDK routines for the native C engine. These do not emulate SH-4
 * registers, and do not supply the SDK operations that still lack a backend. */
#ifndef MVC2_NATIVE64_SDK_NATIVE_H
#define MVC2_NATIVE64_SDK_NATIVE_H

#include <stdint.h>

/* libindex's original public work areas: 6500 32-byte screen records and
 * 6500 16-byte light records (the extents in both supplied symbol maps). */
#define MVC2_NATIVE_SCREEN_WORK_FLOATS 52000
#define MVC2_NATIVE_LIGHT_WORK_WORDS 26000

extern float ScreenVertexBuffer[MVC2_NATIVE_SCREEN_WORK_FLOATS];
extern int32_t LightFragmentBuffer[MVC2_NATIVE_LIGHT_WORK_WORDS];

void nlSysMetricsInit(void);
void nlSysMetricsInitSetClock(int32_t clock);
void nlSysMetricsCpuPowerStart(void);
int32_t nlSysMetricsCpuPowerGet(void);
int32_t nlSysMetricsCpuPowerEnd(void);

/* The original lookup algorithm, in 256ths of a turn, including its rounded
 * ratio and octant boundaries. The undefined zero vector returns zero here. */
int32_t nlAtan256(float y, float x);

/* Resets the original work cursors and native renderer scratch. The renderer
 * retains its typed scratch arrays rather than overlaying native records on
 * the original SDK's 32-byte and 16-byte record formats. NULL chooses the
 * corresponding public work area above. */
void nlInitDrawModel(float* draw_vertex_work, int32_t* draw_light_work);
void nlLightInit(void);

#endif
