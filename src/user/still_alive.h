// SPDX-License-Identifier: MIT
/*
 * src/user/still_alive.h - Note data for Portal's "Still Alive" PC speaker melody
 */
#ifndef STILL_ALIVE_H
#define STILL_ALIVE_H
#include "lib/stdint.h"
#define SA_E 250
#define SA_Q 500
#define SA_DQ 750
#define SA_H 1000
#define SA_DH 1500
#define SA_W 2000
#define SA_A4 440
#define SA_Bb4 466
#define SA_B4 494
#define SA_C5 523
#define SA_Cs5 554
#define SA_D5 587
#define SA_E5 659
#define SA_F5 698
#define SA_Fs5 740
#define SA_G5 784
#define SA_A5 880
#define SA_Bb5 932
#define SA_B5 988
#ifndef NOTE_T_DEFINED
#define NOTE_T_DEFINED
typedef struct {
    uint32_t freq;
    uint32_t ms;
} note_t;
#endif
static const note_t still_alive[] = {
    {SA_G5, SA_E},   {SA_Fs5, SA_E},  {SA_E5, SA_E},   {SA_E5, SA_E},   {SA_Fs5, SA_Q},
    {0, SA_Q},       {0, SA_H},       {0, SA_Q},       {0, SA_E},       {SA_A4, SA_E},
    {SA_G5, SA_E},   {SA_Fs5, SA_E},  {SA_E5, SA_E},   {SA_E5, SA_Q},   {SA_Fs5, SA_E},
    {0, SA_Q},       {SA_D5, SA_Q},   {SA_E5, SA_E},   {SA_A4, SA_E},   {0, SA_H},
    {0, SA_Q},       {0, SA_E},       {SA_A4, SA_E},   {SA_E5, SA_Q},   {SA_Fs5, SA_E},
    {SA_G5, SA_DQ},  {SA_E5, SA_E},   {SA_Cs5, SA_Q},  {SA_D5, SA_DQ},  {SA_E5, SA_Q},
    {SA_A4, SA_E},   {SA_A4, SA_Q},   {SA_Fs5, SA_Q},  {0, SA_E},       {0, SA_H},
    {0, SA_H},       {SA_G5, SA_E},   {SA_Fs5, SA_E},  {SA_E5, SA_E},   {SA_E5, SA_E},
    {SA_Fs5, SA_Q},  {0, SA_Q},       {0, SA_H},       {0, SA_Q},       {0, SA_E},
    {SA_A4, SA_E},   {SA_G5, SA_E},   {SA_Fs5, SA_E},  {SA_E5, SA_E},   {SA_E5, SA_E},
    {0, SA_Q},       {SA_Fs5, SA_E},  {SA_D5, SA_E},   {0, SA_Q},       {SA_E5, SA_E},
    {SA_A4, SA_E},   {0, SA_W},       {SA_E5, SA_Q},   {SA_Fs5, SA_E},  {SA_G5, SA_DQ},
    {SA_E5, SA_E},   {SA_Cs5, SA_Q},  {SA_D5, SA_E},   {SA_E5, SA_E},   {0, SA_E},
    {SA_A4, SA_E},   {SA_D5, SA_E},   {SA_E5, SA_E},   {SA_F5, SA_E},   {SA_E5, SA_E},
    {SA_D5, SA_E},   {SA_C5, SA_E},   {0, SA_Q},       {SA_A4, SA_E},   {SA_Bb4, SA_E},
    {SA_C5, SA_Q},   {SA_F5, SA_Q},   {SA_E5, SA_E},   {SA_D5, SA_E},   {SA_D5, SA_E},
    {SA_C5, SA_E},   {SA_D5, SA_E},   {SA_C5, SA_E},   {SA_C5, SA_Q},   {SA_C5, SA_Q},
    {SA_A4, SA_E},   {SA_Bb4, SA_E},  {SA_C5, SA_Q},   {SA_F5, SA_Q},   {SA_G5, SA_E},
    {SA_F5, SA_E},   {SA_E5, SA_E},   {SA_D5, SA_E},   {SA_D5, SA_E},   {SA_E5, SA_E},
    {SA_F5, SA_Q},   {SA_F5, SA_Q},   {SA_G5, SA_E},   {SA_A5, SA_E},   {SA_Bb5, SA_E},
    {SA_Bb5, SA_E},  {SA_A5, SA_Q},   {SA_G5, SA_Q},   {SA_F5, SA_E},   {SA_G5, SA_E},
    {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_G5, SA_Q},   {SA_F5, SA_Q},   {SA_D5, SA_E},
    {SA_C5, SA_E},   {SA_D5, SA_E},   {SA_F5, SA_E},   {SA_F5, SA_E},   {SA_E5, SA_Q},
    {SA_E5, SA_E},   {SA_Fs5, SA_E},  {SA_Fs5, SA_DQ}, {0, SA_W},       {0, SA_H},
    {SA_G5, SA_E},   {SA_Fs5, SA_E},  {SA_E5, SA_E},   {SA_E5, SA_DQ},  {SA_Fs5, SA_E},
    {SA_D5, SA_Q},   {SA_E5, SA_Q},   {SA_A4, SA_DQ},  {0, SA_Q},       {0, SA_H},
    {SA_E5, SA_Q},   {SA_Fs5, SA_E},  {SA_G5, SA_DQ},  {SA_E5, SA_Q},   {SA_Cs5, SA_Q},
    {SA_D5, SA_E},   {SA_E5, SA_DQ},  {SA_A4, SA_E},   {SA_A4, SA_DQ},  {SA_Fs5, SA_Q},
    {0, SA_E},       {0, SA_H},       {0, SA_Q},       {0, SA_E},       {SA_A4, SA_E},
    {SA_G5, SA_E},   {SA_Fs5, SA_E},  {SA_E5, SA_E},   {SA_E5, SA_E},   {SA_Fs5, SA_Q},
    {0, SA_Q},       {0, SA_Q},       {0, SA_E},       {0, SA_E},       {0, SA_Q},
    {0, SA_E},       {SA_A4, SA_E},   {SA_G5, SA_E},   {SA_Fs5, SA_E},  {SA_E5, SA_E},
    {SA_E5, SA_E},   {0, SA_Q},       {SA_Fs5, SA_E},  {SA_D5, SA_E},   {0, SA_Q},
    {SA_E5, SA_E},   {SA_A4, SA_E},   {0, SA_W},       {SA_E5, SA_Q},   {SA_Fs5, SA_E},
    {SA_G5, SA_DQ},  {SA_E5, SA_Q},   {SA_Cs5, SA_Q},  {SA_D5, SA_E},   {SA_E5, SA_E},
    {0, SA_E},       {SA_A4, SA_E},   {SA_D5, SA_E},   {SA_E5, SA_E},   {SA_F5, SA_E},
    {SA_E5, SA_E},   {SA_D5, SA_E},   {SA_C5, SA_E},   {0, SA_Q},       {SA_A4, SA_E},
    {SA_Bb4, SA_E},  {SA_C5, SA_Q},   {SA_F5, SA_Q},   {SA_E5, SA_E},   {SA_D5, SA_E},
    {SA_D5, SA_E},   {SA_C5, SA_E},   {SA_D5, SA_E},   {SA_C5, SA_E},   {SA_C5, SA_Q},
    {SA_C5, SA_Q},   {SA_A4, SA_E},   {SA_Bb4, SA_E},  {SA_C5, SA_Q},   {SA_F5, SA_Q},
    {SA_G5, SA_E},   {SA_F5, SA_E},   {SA_E5, SA_E},   {SA_D5, SA_E},   {SA_D5, SA_E},
    {SA_E5, SA_E},   {SA_F5, SA_Q},   {SA_F5, SA_Q},   {SA_G5, SA_E},   {SA_A5, SA_E},
    {SA_Bb5, SA_E},  {SA_Bb5, SA_E},  {SA_A5, SA_Q},   {SA_G5, SA_Q},   {SA_F5, SA_E},
    {SA_G5, SA_E},   {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_G5, SA_E},   {SA_F5, SA_E},
    {SA_F5, SA_Q},   {SA_D5, SA_E},   {SA_C5, SA_E},   {SA_D5, SA_E},   {SA_F5, SA_E},
    {SA_F5, SA_E},   {SA_E5, SA_Q},   {SA_E5, SA_E},   {SA_Fs5, SA_Q},  {SA_Fs5, SA_DQ},
    {0, SA_W},       {0, SA_H},       {0, SA_E},       {SA_A4, SA_E},   {SA_G5, SA_E},
    {SA_Fs5, SA_E},  {SA_E5, SA_E},   {SA_E5, SA_E},   {0, SA_Q},       {SA_Fs5, SA_E},
    {SA_D5, SA_E},   {0, SA_Q},       {SA_E5, SA_E},   {SA_A4, SA_E},   {0, SA_W},
    {SA_E5, SA_Q},   {SA_Fs5, SA_E},  {SA_G5, SA_DQ},  {SA_E5, SA_Q},   {SA_Cs5, SA_Q},
    {SA_D5, SA_E},   {SA_E5, SA_DQ},  {SA_A4, SA_E},   {SA_A4, SA_DQ},  {SA_Fs5, SA_Q},
    {0, SA_E},       {0, SA_H},       {0, SA_H},       {SA_G5, SA_E},   {SA_Fs5, SA_E},
    {SA_E5, SA_E},   {SA_E5, SA_DQ},  {SA_Fs5, SA_Q},  {0, SA_E},       {0, SA_H},
    {0, SA_H},       {SA_G5, SA_E},   {SA_Fs5, SA_E},  {SA_E5, SA_E},   {SA_E5, SA_E},
    {0, SA_Q},       {SA_Fs5, SA_E},  {SA_D5, SA_E},   {0, SA_Q},       {SA_E5, SA_E},
    {SA_A4, SA_E},   {0, SA_W},       {SA_E5, SA_Q},   {SA_Fs5, SA_E},  {SA_G5, SA_DQ},
    {SA_E5, SA_Q},   {SA_Cs5, SA_Q},  {SA_D5, SA_E},   {SA_E5, SA_E},   {0, SA_E},
    {SA_A4, SA_E},   {SA_D5, SA_E},   {SA_E5, SA_E},   {SA_F5, SA_E},   {SA_E5, SA_E},
    {SA_D5, SA_E},   {SA_C5, SA_E},   {0, SA_Q},       {SA_A4, SA_E},   {SA_Bb4, SA_E},
    {SA_C5, SA_Q},   {SA_F5, SA_Q},   {SA_E5, SA_E},   {SA_D5, SA_E},   {SA_D5, SA_E},
    {SA_C5, SA_E},   {SA_D5, SA_E},   {SA_C5, SA_E},   {SA_C5, SA_Q},   {SA_C5, SA_Q},
    {SA_A4, SA_E},   {SA_Bb4, SA_E},  {SA_C5, SA_Q},   {SA_F5, SA_Q},   {SA_G5, SA_E},
    {SA_F5, SA_E},   {SA_E5, SA_E},   {SA_D5, SA_E},   {SA_D5, SA_E},   {SA_E5, SA_E},
    {SA_F5, SA_Q},   {SA_F5, SA_Q},   {SA_G5, SA_E},   {SA_A5, SA_E},   {SA_Bb5, SA_E},
    {SA_Bb5, SA_E},  {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_G5, SA_Q},   {SA_F5, SA_E},
    {SA_G5, SA_E},   {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_G5, SA_E},   {SA_G5, SA_E},
    {SA_F5, SA_Q},   {SA_D5, SA_E},   {SA_C5, SA_E},   {SA_D5, SA_E},   {SA_F5, SA_E},
    {SA_F5, SA_E},   {SA_E5, SA_Q},   {SA_E5, SA_E},   {SA_Fs5, SA_E},  {SA_Fs5, SA_DH},
    {0, SA_Q},       {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_B5, SA_E},   {SA_A5, SA_E},
    {SA_Fs5, SA_E},  {SA_D5, SA_Q},   {SA_E5, SA_E},   {SA_Fs5, SA_E},  {SA_Fs5, SA_DQ},
    {0, SA_Q},       {0, SA_E},       {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_A5, SA_E},
    {SA_B5, SA_E},   {SA_A5, SA_E},   {SA_Fs5, SA_E},  {SA_D5, SA_Q},   {SA_E5, SA_E},
    {SA_Fs5, SA_E},  {SA_Fs5, SA_DQ}, {0, SA_Q},       {0, SA_E},       {SA_A5, SA_E},
    {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_B5, SA_E},   {SA_A5, SA_E},   {SA_Fs5, SA_E},
    {SA_D5, SA_Q},   {SA_E5, SA_E},   {SA_Fs5, SA_E},  {SA_Fs5, SA_DQ}, {0, SA_H},
    {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_B5, SA_E},   {SA_A5, SA_E},   {SA_Fs5, SA_E},
    {SA_D5, SA_Q},   {SA_E5, SA_E},   {SA_Fs5, SA_E},  {SA_Fs5, SA_DQ}, {0, SA_Q},
    {0, SA_E},       {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_A5, SA_E},   {SA_B5, SA_E},
    {SA_A5, SA_E},   {SA_Fs5, SA_E},  {SA_D5, SA_Q},   {SA_E5, SA_E},   {SA_Fs5, SA_E},
    {SA_Fs5, SA_DQ}, {0, SA_Q},       {0, SA_E},       {SA_Fs5, SA_E},  {SA_A5, SA_E},
    {SA_A5, SA_DQ},  {0, SA_Q},       {0, SA_E},       {SA_G5, SA_E},   {SA_Fs5, SA_E},
    {SA_Fs5, SA_DQ}, {0, SA_E},       {0, SA_H},       {0, SA_W},
};
#undef SA_E
#undef SA_Q
#undef SA_DQ
#undef SA_H
#undef SA_DH
#undef SA_W
#undef SA_A4
#undef SA_Bb4
#undef SA_B4
#undef SA_C5
#undef SA_Cs5
#undef SA_D5
#undef SA_E5
#undef SA_F5
#undef SA_Fs5
#undef SA_G5
#undef SA_A5
#undef SA_Bb5
#undef SA_B5
#endif
