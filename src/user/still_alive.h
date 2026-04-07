/*
 * still_alive.h — "Still Alive" by Jonathan Coulton (Portal)
 *
 * Source: Flipper Zero transcription by Will-Beninger
 *         https://gist.github.com/Will-Beninger/fd8c53a22853753a92792a6d90edb3f9
 *
 * Format: note_t { uint32_t freq; uint32_t ms; }
 *   freq = 0  → rest
 *   Durations at 120 BPM; Flipper octave 5 = standard 4, default oct 6 = standard 5
 */

#ifndef STILL_ALIVE_H
#define STILL_ALIVE_H

#include "lib/stdint.h"

/* Durations (ms) at 120 BPM */
#define SA_E   250   /* eighth */
#define SA_Q   500   /* quarter */
#define SA_DQ  750   /* dotted quarter */
#define SA_H  1000   /* half */
#define SA_DH 1500   /* dotted half */
#define SA_W  2000   /* whole */

/* Note frequencies — standard octave numbering */
#define SA_A4   440
#define SA_Bb4  466
#define SA_B4   494
#define SA_C5   523
#define SA_Cs5  554
#define SA_D5   587
#define SA_E5   659
#define SA_F5   698
#define SA_Fs5  740
#define SA_G5   784
#define SA_A5   880
#define SA_Bb5  932
#define SA_B5   988

#ifndef NOTE_T_DEFINED
#define NOTE_T_DEFINED
typedef struct { uint32_t freq; uint32_t ms; } note_t;
#endif

static const note_t still_alive[] = {
    /* === VERSE 1: "This was a triumph / I'm making a note here" === */
    {SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_E},{SA_Fs5,SA_Q},{0,SA_Q},{0,SA_H},{0,SA_Q},
    {0,SA_E},{SA_A4,SA_E},{SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_Q},{SA_Fs5,SA_E},{0,SA_Q},{SA_D5,SA_Q},
    {SA_E5,SA_E},{SA_A4,SA_E},{0,SA_H},{0,SA_Q},
    {0,SA_E},{SA_A4,SA_E},{SA_E5,SA_Q},{SA_Fs5,SA_E},{SA_G5,SA_DQ},
    {SA_E5,SA_E},{SA_Cs5,SA_Q},{SA_D5,SA_DQ},
    {SA_E5,SA_Q},{SA_A4,SA_E},{SA_A4,SA_Q},{SA_Fs5,SA_Q},{0,SA_E},{0,SA_H},{0,SA_H},

    /* === VERSE 2: "Aperture Science / We do what we must" === */
    {SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_E},{SA_Fs5,SA_Q},{0,SA_Q},{0,SA_H},{0,SA_Q},
    {0,SA_E},{SA_A4,SA_E},{SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_E},{0,SA_Q},{SA_Fs5,SA_E},{SA_D5,SA_E},{0,SA_Q},{SA_E5,SA_E},{SA_A4,SA_E},{0,SA_W},
    {SA_E5,SA_Q},{SA_Fs5,SA_E},{SA_G5,SA_DQ},
    {SA_E5,SA_E},{SA_Cs5,SA_Q},{SA_D5,SA_E},{SA_E5,SA_E},{0,SA_E},{SA_A4,SA_E},

    /* === CHORUS 1: "But there's no sense crying" === */
    {SA_D5,SA_E},{SA_E5,SA_E},{SA_F5,SA_E},{SA_E5,SA_E},{SA_D5,SA_E},{SA_C5,SA_E},{0,SA_Q},
    {SA_A4,SA_E},{SA_Bb4,SA_E},{SA_C5,SA_Q},{SA_F5,SA_Q},{SA_E5,SA_E},{SA_D5,SA_E},
    {SA_D5,SA_E},{SA_C5,SA_E},{SA_D5,SA_E},{SA_C5,SA_E},{SA_C5,SA_Q},{SA_C5,SA_Q},
    {SA_A4,SA_E},{SA_Bb4,SA_E},{SA_C5,SA_Q},{SA_F5,SA_Q},{SA_G5,SA_E},{SA_F5,SA_E},{SA_E5,SA_E},{SA_D5,SA_E},
    {SA_D5,SA_E},{SA_E5,SA_E},{SA_F5,SA_Q},{SA_F5,SA_Q},
    {SA_G5,SA_E},{SA_A5,SA_E},{SA_Bb5,SA_E},{SA_Bb5,SA_E},{SA_A5,SA_Q},{SA_G5,SA_Q},
    {SA_F5,SA_E},{SA_G5,SA_E},{SA_A5,SA_E},{SA_A5,SA_E},{SA_G5,SA_Q},{SA_F5,SA_Q},
    {SA_D5,SA_E},{SA_C5,SA_E},{SA_D5,SA_E},{SA_F5,SA_E},{SA_F5,SA_E},{SA_E5,SA_Q},{SA_E5,SA_E},{SA_Fs5,SA_E},{SA_Fs5,SA_DQ},

    /* === BREAK 1 === */
    {0,SA_W},{0,SA_H},

    /* === VERSE 3: "I'm not even angry" === */
    {SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_DQ},
    {SA_Fs5,SA_E},{SA_D5,SA_Q},{SA_E5,SA_Q},{SA_A4,SA_DQ},{0,SA_Q},{0,SA_H},
    {SA_E5,SA_Q},{SA_Fs5,SA_E},{SA_G5,SA_DQ},
    {SA_E5,SA_Q},{SA_Cs5,SA_Q},{SA_D5,SA_E},{SA_E5,SA_DQ},
    {SA_A4,SA_E},{SA_A4,SA_DQ},{SA_Fs5,SA_Q},{0,SA_E},{0,SA_H},{0,SA_Q},

    /* === VERSE 4: "Go ahead and leave me" === */
    {0,SA_E},{SA_A4,SA_E},{SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_E},{SA_Fs5,SA_Q},{0,SA_Q},{0,SA_Q},
    {0,SA_E},{0,SA_E},{0,SA_Q},{0,SA_E},{SA_A4,SA_E},{SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_E},{0,SA_Q},{SA_Fs5,SA_E},{SA_D5,SA_E},{0,SA_Q},{SA_E5,SA_E},{SA_A4,SA_E},{0,SA_W},
    {SA_E5,SA_Q},{SA_Fs5,SA_E},{SA_G5,SA_DQ},
    {SA_E5,SA_Q},{SA_Cs5,SA_Q},{SA_D5,SA_E},{SA_E5,SA_E},{0,SA_E},{SA_A4,SA_E},

    /* === CHORUS 2 === */
    {SA_D5,SA_E},{SA_E5,SA_E},{SA_F5,SA_E},{SA_E5,SA_E},{SA_D5,SA_E},{SA_C5,SA_E},{0,SA_Q},
    {SA_A4,SA_E},{SA_Bb4,SA_E},{SA_C5,SA_Q},{SA_F5,SA_Q},{SA_E5,SA_E},{SA_D5,SA_E},
    {SA_D5,SA_E},{SA_C5,SA_E},{SA_D5,SA_E},{SA_C5,SA_E},{SA_C5,SA_Q},{SA_C5,SA_Q},
    {SA_A4,SA_E},{SA_Bb4,SA_E},{SA_C5,SA_Q},{SA_F5,SA_Q},{SA_G5,SA_E},{SA_F5,SA_E},{SA_E5,SA_E},{SA_D5,SA_E},
    {SA_D5,SA_E},{SA_E5,SA_E},{SA_F5,SA_Q},{SA_F5,SA_Q},
    {SA_G5,SA_E},{SA_A5,SA_E},{SA_Bb5,SA_E},{SA_Bb5,SA_E},{SA_A5,SA_Q},{SA_G5,SA_Q},
    {SA_F5,SA_E},{SA_G5,SA_E},{SA_A5,SA_E},{SA_A5,SA_E},{SA_G5,SA_E},{SA_F5,SA_E},{SA_F5,SA_Q},
    {SA_D5,SA_E},{SA_C5,SA_E},{SA_D5,SA_E},{SA_F5,SA_E},{SA_F5,SA_E},{SA_E5,SA_Q},{SA_E5,SA_E},{SA_Fs5,SA_Q},{SA_Fs5,SA_DQ},

    /* === BREAK 2 === */
    {0,SA_W},{0,SA_H},

    /* === VERSE 5: "I've experiments to run" === */
    {0,SA_E},{SA_A4,SA_E},{SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_E},{0,SA_Q},{SA_Fs5,SA_E},{SA_D5,SA_E},{0,SA_Q},{SA_E5,SA_E},{SA_A4,SA_E},{0,SA_W},
    {SA_E5,SA_Q},{SA_Fs5,SA_E},{SA_G5,SA_DQ},
    {SA_E5,SA_Q},{SA_Cs5,SA_Q},{SA_D5,SA_E},{SA_E5,SA_DQ},
    {SA_A4,SA_E},{SA_A4,SA_DQ},{SA_Fs5,SA_Q},{0,SA_E},{0,SA_H},{0,SA_H},

    /* === VERSE 6: "I'm being so sincere right now" === */
    {SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_DQ},{SA_Fs5,SA_Q},{0,SA_E},{0,SA_H},{0,SA_H},

    /* === VERSE 7 / final pre-chorus === */
    {SA_G5,SA_E},{SA_Fs5,SA_E},{SA_E5,SA_E},{SA_E5,SA_E},{0,SA_Q},{SA_Fs5,SA_E},{SA_D5,SA_E},{0,SA_Q},{SA_E5,SA_E},{SA_A4,SA_E},{0,SA_W},
    {SA_E5,SA_Q},{SA_Fs5,SA_E},{SA_G5,SA_DQ},
    {SA_E5,SA_Q},{SA_Cs5,SA_Q},{SA_D5,SA_E},{SA_E5,SA_E},{0,SA_E},{SA_A4,SA_E},

    /* === CHORUS 3 (final) === */
    {SA_D5,SA_E},{SA_E5,SA_E},{SA_F5,SA_E},{SA_E5,SA_E},{SA_D5,SA_E},{SA_C5,SA_E},{0,SA_Q},
    {SA_A4,SA_E},{SA_Bb4,SA_E},{SA_C5,SA_Q},{SA_F5,SA_Q},{SA_E5,SA_E},{SA_D5,SA_E},
    {SA_D5,SA_E},{SA_C5,SA_E},{SA_D5,SA_E},{SA_C5,SA_E},{SA_C5,SA_Q},{SA_C5,SA_Q},
    {SA_A4,SA_E},{SA_Bb4,SA_E},{SA_C5,SA_Q},{SA_F5,SA_Q},{SA_G5,SA_E},{SA_F5,SA_E},{SA_E5,SA_E},{SA_D5,SA_E},
    {SA_D5,SA_E},{SA_E5,SA_E},{SA_F5,SA_Q},{SA_F5,SA_Q},
    {SA_G5,SA_E},{SA_A5,SA_E},{SA_Bb5,SA_E},{SA_Bb5,SA_E},{SA_A5,SA_E},{SA_A5,SA_E},{SA_G5,SA_Q},
    {SA_F5,SA_E},{SA_G5,SA_E},{SA_A5,SA_E},{SA_A5,SA_E},{SA_G5,SA_E},{SA_G5,SA_E},{SA_F5,SA_Q},
    {SA_D5,SA_E},{SA_C5,SA_E},{SA_D5,SA_E},{SA_F5,SA_E},{SA_F5,SA_E},{SA_E5,SA_Q},{SA_E5,SA_E},{SA_Fs5,SA_E},{SA_Fs5,SA_DH},{0,SA_Q},

    /* === OUTRO: "I'm still alive" × 5 === */
    {SA_A5,SA_E},{SA_A5,SA_E},{SA_B5,SA_E},{SA_A5,SA_E},{SA_Fs5,SA_E},{SA_D5,SA_Q},{SA_E5,SA_E},{SA_Fs5,SA_E},{SA_Fs5,SA_DQ},{0,SA_Q},{0,SA_E},
    {SA_A5,SA_E},{SA_A5,SA_E},{SA_A5,SA_E},{SA_B5,SA_E},{SA_A5,SA_E},{SA_Fs5,SA_E},{SA_D5,SA_Q},{SA_E5,SA_E},{SA_Fs5,SA_E},{SA_Fs5,SA_DQ},{0,SA_Q},{0,SA_E},
    {SA_A5,SA_E},{SA_A5,SA_E},{SA_A5,SA_E},{SA_B5,SA_E},{SA_A5,SA_E},{SA_Fs5,SA_E},{SA_D5,SA_Q},{SA_E5,SA_E},{SA_Fs5,SA_E},{SA_Fs5,SA_DQ},{0,SA_H},
    {SA_A5,SA_E},{SA_A5,SA_E},{SA_B5,SA_E},{SA_A5,SA_E},{SA_Fs5,SA_E},{SA_D5,SA_Q},{SA_E5,SA_E},{SA_Fs5,SA_E},{SA_Fs5,SA_DQ},{0,SA_Q},{0,SA_E},
    {SA_A5,SA_E},{SA_A5,SA_E},{SA_A5,SA_E},{SA_B5,SA_E},{SA_A5,SA_E},{SA_Fs5,SA_E},{SA_D5,SA_Q},{SA_E5,SA_E},{SA_Fs5,SA_E},{SA_Fs5,SA_DQ},{0,SA_Q},{0,SA_E},
    {SA_Fs5,SA_E},{SA_A5,SA_E},{SA_A5,SA_DQ},{0,SA_Q},{0,SA_E},{SA_G5,SA_E},{SA_Fs5,SA_E},{SA_Fs5,SA_DQ},{0,SA_E},{0,SA_H},{0,SA_W},
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

#endif /* STILL_ALIVE_H */
