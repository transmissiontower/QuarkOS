// SPDX-License-Identifier: MIT
/*
 * src/drivers/ps2/mouse.h - PS/2 mouse driver interface and state globals
 */
#ifndef MOUSE_H
#define MOUSE_H
#include "lib/stdint.h"
#include "util/util.h"
extern int32_t mouse_x;
extern int32_t mouse_y;
extern int8_t mouse_left;
extern int8_t mouse_right;
extern int8_t mouse_middle;
void drv_input_mouse_init();
#endif
