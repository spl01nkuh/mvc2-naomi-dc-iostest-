/* Native descriptors shared by game C and its graphics backend. Serialized
 * file records remain 16/12-byte words and must be decoded separately. */
#ifndef MVC2_HOST_ASSETS_H
#define MVC2_HOST_ASSETS_H
#include <stdint.h>
typedef struct NLthd {
    int16_t width, height;
    int32_t type;
    void* address;
    int32_t globl_index;
} NLthd;
typedef struct OBJ_FILE {
    int** obj;
    int32_t cnt;
    NLthd* thd;
} MVC2HostObjectFile;
#endif
