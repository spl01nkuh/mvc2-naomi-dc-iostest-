/*
 * nl_work.c  nlsystem's work that nlproj.c reads, the host's since both programs share the library
 */

#include "nlsystem.h"
#include "host_sections.h"

NLSYSWORK NLSystemWork_ = { .scale_factor = 1.0f };
NLSCREEN nl_scr;
Sint32 nl_VGA_flag, nl_PAL_EXT_flag, nl_ext_pal_height;
Uint8 nl_objput_param[0x2c];        /* NLOBJPUTPARAM */

/* nlSetScaleFactor's scale, which the next model drawn takes */
__attribute__((section(MVC2_NL_HOT_SECTION))) float nl_take_scale_factor(void) {
    float s = NLSystemWork_.scale_factor;
    NLSystemWork_.scale_factor = 1.0f;
    return s;
}
