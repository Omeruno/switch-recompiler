#ifndef FREAKOUT_TEXTURE_VIEW_H
#define FREAKOUT_TEXTURE_VIEW_H

#include <deko3d.h>

enum { VIEW_WIDTH = 1024, VIEW_HEIGHT = 512 };

typedef struct {
    DkMemBlock code, descriptors, storage;
    DkImage image;
    DkShader vertex, fragment;
} TextureView;

const char *texture_view_init(TextureView *view, DkDevice device, DkQueue queue,
                              DkCmdBuf commands, DkMemBlock command_memory, uint32_t command_bytes);
void texture_view_draw(const TextureView *view, DkCmdBuf commands);
void texture_view_destroy(TextureView *view);

#endif
