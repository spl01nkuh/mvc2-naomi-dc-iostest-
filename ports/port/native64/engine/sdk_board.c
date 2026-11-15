/* Native equivalents of the board startup state used by system.c. */
#include "naomi_host.h"
#include <string.h>

/* libjamma's public counters; the host's JVS input has no transmission faults. */
int nlaJAMMAAcknowError[31][8];

extern void nlInitInterrupt(void);
extern void syTmrGenSetClock(int clock);
extern void syTmrGenSetCount(unsigned count);

void syHwInit_NAOMI(void) {
    memset(nlaJAMMAAcknowError, 0, sizeof nlaJAMMAAcknowError);
    nlInitInterrupt();
    syTmrGenSetClock(4);
    syTmrGenSetCount(0);
}

/* The software renderer currently has no PowerVR modifier-volume stencil.
 * Keep the typed state and report its actual unsupported draw explicitly. */
static int modifier_intensity = 128;
static int modifier_mode;
static int modifier_reported;

void nlObjPutMvInit(int intensity) {
    modifier_intensity = intensity;
    modifier_mode = 0;
    modifier_reported = 0;
}

void nlObjPutMvSetIntensity(int intensity) {
    modifier_intensity = intensity;
}

void nlObjPutMvSetMode(int mode) {
    modifier_mode = mode;
}

void nlObjPutMvClosedVolumeUseViewMatrix(int* object) {
    if (object && !modifier_reported) {
        host_log("modifier-volume shadows unavailable in the software renderer (mode %d, intensity %d)",
                 modifier_mode, modifier_intensity);
        modifier_reported = 1;
    }
}
