// SPDX-License-Identifier: MIT
/*
 * src/user/rick_roll.h - Note data for "Never Gonna Give You Up" PC speaker melody
 */
#ifndef RICK_ROLL_H
#define RICK_ROLL_H
#include "lib/stdint.h"
#ifndef NOTE_T_DEFINED
#define NOTE_T_DEFINED
typedef struct {
    uint32_t freq;
    uint32_t ms;
} note_t;
#endif
#define RK_W 2100
#define RK_H 1050
#define RK_Q 530
#define RK_E 260
#define RK_S 130
#define RK_DQ 790
#define RK_DE 390
#define RK_DH 1580
#define RK_A4 440
#define RK_B4 494
#define RK_CS5 554
#define RK_D5 587
#define RK_E5 659
#define RK_FS5 740
#define RK_G5 784
#define RK_A5 880
static const note_t rick_roll[] = {
    {RK_D5, RK_DQ},  {RK_E5, RK_DQ},  {RK_A4, RK_Q},   {RK_E5, RK_DQ},  {RK_FS5, RK_DQ},
    {RK_A5, RK_S},   {RK_G5, RK_S},   {RK_FS5, RK_E},  {RK_D5, RK_DQ},  {RK_E5, RK_DQ},
    {RK_A4, RK_H},   {RK_A4, RK_S},   {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_E},
    {RK_D5, RK_S},   {RK_D5, RK_DQ},  {RK_E5, RK_DQ},  {RK_A4, RK_Q},   {RK_E5, RK_DQ},
    {RK_FS5, RK_DQ}, {RK_A5, RK_S},   {RK_G5, RK_S},   {RK_FS5, RK_E},  {RK_D5, RK_DQ},
    {RK_E5, RK_DQ},  {RK_A4, RK_H},   {RK_A4, RK_S},   {RK_A4, RK_S},   {RK_B4, RK_S},
    {RK_D5, RK_E},   {RK_D5, RK_S},   {0, RK_Q},       {RK_B4, RK_E},   {RK_CS5, RK_E},
    {RK_D5, RK_E},   {RK_D5, RK_E},   {RK_E5, RK_E},   {RK_CS5, RK_DE}, {RK_B4, RK_S},
    {RK_A4, RK_H},   {0, RK_Q},       {0, RK_E},       {RK_B4, RK_E},   {RK_B4, RK_E},
    {RK_CS5, RK_E},  {RK_D5, RK_E},   {RK_B4, RK_Q},   {RK_A4, RK_E},   {RK_A5, RK_E},
    {0, RK_E},       {RK_A5, RK_E},   {RK_E5, RK_DQ},  {0, RK_Q},       {RK_B4, RK_E},
    {RK_B4, RK_E},   {RK_CS5, RK_E},  {RK_D5, RK_E},   {RK_B4, RK_E},   {RK_D5, RK_E},
    {RK_E5, RK_E},   {0, RK_E},       {0, RK_E},       {RK_CS5, RK_E},  {RK_B4, RK_E},
    {RK_A4, RK_DQ},  {0, RK_Q},       {0, RK_E},       {RK_B4, RK_E},   {RK_B4, RK_E},
    {RK_CS5, RK_E},  {RK_D5, RK_E},   {RK_B4, RK_E},   {RK_A4, RK_Q},   {RK_E5, RK_E},
    {RK_E5, RK_E},   {RK_E5, RK_E},   {RK_FS5, RK_E},  {RK_E5, RK_Q},   {0, RK_Q},
    {RK_D5, RK_H},   {RK_E5, RK_E},   {RK_FS5, RK_E},  {RK_D5, RK_E},   {RK_E5, RK_E},
    {RK_E5, RK_E},   {RK_E5, RK_E},   {RK_FS5, RK_E},  {RK_E5, RK_Q},   {RK_A4, RK_Q},
    {0, RK_H},       {RK_B4, RK_E},   {RK_CS5, RK_E},  {RK_D5, RK_E},   {RK_B4, RK_E},
    {0, RK_E},       {RK_E5, RK_E},   {RK_FS5, RK_E},  {RK_E5, RK_DQ},  {RK_A4, RK_S},
    {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_FS5, RK_DE}, {RK_FS5, RK_DE},
    {RK_E5, RK_DQ},  {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},
    {RK_E5, RK_DE},  {RK_E5, RK_DE},  {RK_D5, RK_DE},  {RK_CS5, RK_S},  {RK_B4, RK_DE},
    {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_Q},
    {RK_E5, RK_E},   {RK_CS5, RK_DE}, {RK_B4, RK_S},   {RK_A4, RK_E},   {RK_A4, RK_E},
    {RK_A4, RK_E},   {RK_E5, RK_Q},   {RK_D5, RK_H},   {RK_A4, RK_S},   {RK_B4, RK_S},
    {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_FS5, RK_DE}, {RK_FS5, RK_DE}, {RK_E5, RK_DQ},
    {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_A5, RK_Q},
    {RK_CS5, RK_E},  {RK_D5, RK_DE},  {RK_CS5, RK_S},  {RK_B4, RK_E},   {RK_A4, RK_S},
    {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_Q},   {RK_E5, RK_E},
    {RK_CS5, RK_DE}, {RK_B4, RK_S},   {RK_A4, RK_Q},   {RK_A4, RK_E},   {RK_E5, RK_Q},
    {RK_D5, RK_H},   {0, RK_Q},       {0, RK_E},       {RK_B4, RK_E},   {RK_D5, RK_E},
    {RK_B4, RK_E},   {RK_D5, RK_E},   {RK_E5, RK_Q},   {0, RK_E},       {0, RK_E},
    {RK_CS5, RK_E},  {RK_B4, RK_E},   {RK_A4, RK_DQ},  {0, RK_Q},       {0, RK_E},
    {RK_B4, RK_E},   {RK_B4, RK_E},   {RK_CS5, RK_E},  {RK_D5, RK_E},   {RK_B4, RK_E},
    {RK_A4, RK_Q},   {0, RK_E},       {RK_A5, RK_E},   {RK_A5, RK_E},   {RK_E5, RK_E},
    {RK_FS5, RK_E},  {RK_E5, RK_E},   {RK_D5, RK_E},   {0, RK_E},       {RK_A4, RK_E},
    {RK_B4, RK_E},   {RK_CS5, RK_E},  {RK_D5, RK_E},   {RK_B4, RK_E},   {0, RK_E},
    {RK_CS5, RK_E},  {RK_B4, RK_E},   {RK_A4, RK_DQ},  {0, RK_Q},       {RK_B4, RK_E},
    {RK_B4, RK_E},   {RK_CS5, RK_E},  {RK_D5, RK_E},   {RK_B4, RK_E},   {RK_A4, RK_Q},
    {0, RK_E},       {0, RK_E},       {RK_E5, RK_E},   {RK_E5, RK_E},   {RK_FS5, RK_Q},
    {RK_E5, RK_DQ},  {RK_D5, RK_H},   {RK_D5, RK_E},   {RK_E5, RK_E},   {RK_FS5, RK_E},
    {RK_E5, RK_Q},   {RK_E5, RK_E},   {RK_E5, RK_E},   {RK_FS5, RK_E},  {RK_E5, RK_E},
    {RK_A4, RK_E},   {RK_A4, RK_Q},   {0, RK_DQ},      {RK_A4, RK_E},   {RK_B4, RK_E},
    {RK_CS5, RK_E},  {RK_D5, RK_E},   {RK_B4, RK_E},   {0, RK_E},       {RK_E5, RK_E},
    {RK_FS5, RK_E},  {RK_E5, RK_DQ},  {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},
    {RK_B4, RK_S},   {RK_FS5, RK_DE}, {RK_FS5, RK_DE}, {RK_E5, RK_DQ},  {RK_A4, RK_S},
    {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_E5, RK_DE},  {RK_E5, RK_DE},
    {RK_D5, RK_DE},  {RK_CS5, RK_S},  {RK_B4, RK_E},   {RK_A4, RK_S},   {RK_B4, RK_S},
    {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_Q},   {RK_E5, RK_E},   {RK_CS5, RK_DE},
    {RK_B4, RK_S},   {RK_A4, RK_Q},   {RK_A4, RK_E},   {RK_E5, RK_Q},   {RK_D5, RK_H},
    {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_FS5, RK_DE},
    {RK_FS5, RK_DE}, {RK_E5, RK_DQ},  {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},
    {RK_B4, RK_S},   {RK_A5, RK_Q},   {RK_CS5, RK_E},  {RK_D5, RK_DE},  {RK_CS5, RK_S},
    {RK_B4, RK_E},   {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},
    {RK_D5, RK_Q},   {RK_E5, RK_E},   {RK_CS5, RK_DE}, {RK_B4, RK_S},   {RK_A4, RK_Q},
    {RK_A4, RK_E},   {RK_E5, RK_Q},   {RK_D5, RK_H},   {RK_A4, RK_S},   {RK_B4, RK_S},
    {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_FS5, RK_DE}, {RK_FS5, RK_DE}, {RK_E5, RK_DQ},
    {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_A5, RK_Q},
    {RK_CS5, RK_E},  {RK_D5, RK_DE},  {RK_CS5, RK_S},  {RK_B4, RK_E},   {RK_A4, RK_S},
    {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_Q},   {RK_E5, RK_E},
    {RK_CS5, RK_DE}, {RK_B4, RK_S},   {RK_A4, RK_Q},   {RK_A4, RK_E},   {RK_E5, RK_Q},
    {RK_D5, RK_H},   {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},   {RK_B4, RK_S},
    {RK_FS5, RK_DE}, {RK_FS5, RK_DE}, {RK_E5, RK_DQ},  {RK_A4, RK_S},   {RK_B4, RK_S},
    {RK_D5, RK_S},   {RK_B4, RK_S},   {RK_A5, RK_Q},   {RK_CS5, RK_E},  {RK_D5, RK_DE},
    {RK_CS5, RK_S},  {RK_B4, RK_E},   {RK_A4, RK_S},   {RK_B4, RK_S},   {RK_D5, RK_S},
    {RK_B4, RK_S},   {RK_D5, RK_Q},   {RK_E5, RK_E},   {RK_CS5, RK_DE}, {RK_B4, RK_S},
    {RK_A4, RK_Q},   {RK_A4, RK_E},   {RK_E5, RK_Q},   {RK_D5, RK_H},   {0, RK_Q},
};
#undef RK_W
#undef RK_H
#undef RK_Q
#undef RK_E
#undef RK_S
#undef RK_DQ
#undef RK_DE
#undef RK_DH
#undef RK_A4
#undef RK_B4
#undef RK_CS5
#undef RK_D5
#undef RK_E5
#undef RK_FS5
#undef RK_G5
#undef RK_A5
#endif
