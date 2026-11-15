/* Native C entry point. Native tables contain full-width pointers; no ROM
 * relocation list or fixed-address linker alias belongs in this program. */
#include "naomi_host.h"

extern void naomi_main(void);

const HOST_PROGRAM arc_program = { naomi_main, NULL, { NULL, NULL, NULL, NULL } };
