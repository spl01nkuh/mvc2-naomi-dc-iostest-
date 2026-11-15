/*
 * rd_gpu.h  The SDL_GPU back end (SDL3): the frame drawn by the GPU, by rd_soft.c's rules.
 * MVC2_GPULAYERS sets the translucent layers (12, 4 with nosort); MVC2_GPUEDGES=0 the GPU's own edges.
 */

#ifndef RD_GPU_H
#define RD_GPU_H

#include <SDL3/SDL_gpu.h>

/* for SDL_CreateGPUDevice: DXBC for Direct3D 12, SPIR-V for Vulkan */
#define RD_GPU_SHADER_FORMATS (SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_SPIRV)

#define RD_GPU_MAX_SCALE 12

enum { RD_SPRITES_ARCADE, RD_SPRITES_SHARP, RD_SPRITES_PIXEL };

bool rd_gpu_init(SDL_GPUDevice* device);
/* From the next frame: drawn at resolution times the game's 640 by 480 (0: enough for h, times aa), then
   filtered down to w by h when that is smaller (0: not). Drawn larger, a sprite's texture is read as the
   arcade does, sharp (bilinear only across its texels' edges) or by the nearest texel */
void rd_gpu_view(int resolution, int aa, int w, int h, int sprites);
/* the frame drawn by cmd into the back end's texture (R8G8B8A8), in place of rd_end_frame */
SDL_GPUTexture* rd_gpu_end_frame(SDL_GPUCommandBuffer* cmd);
void rd_gpu_frame_size(int* w, int* h);     /* that texture's */
/* the last frame as ARGB, at 640 by 480 / at rd_gpu_frame_size's: waits for the GPU */
bool rd_gpu_read(uint32_t* argb);
bool rd_gpu_read_view(uint32_t* argb);
void rd_gpu_quit(void);

#endif
