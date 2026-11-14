/* Shared matrix-stack ABI for the C backend and native projection routines. */
#ifndef MVC2_HOST_MATRIX_H
#define MVC2_HOST_MATRIX_H
typedef float MVC2HostMatrix[16];
typedef struct MVC2HostMatrixStack {
    short count, max;
    MVC2HostMatrix* base;
    MVC2HostMatrix* matrix;
} MVC2HostMatrixStack;
#endif
