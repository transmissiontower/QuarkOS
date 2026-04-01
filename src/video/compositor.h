#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "video/texture.h"
#include "video/window.h"

#define WIN_SHADOW_SIZE 24

void compositor_init();
void compositor_paint();

void invalidate_screen();
void invalidate_window(int x, int y, int w, int h);

void set_background(Texture *tex);

#endif