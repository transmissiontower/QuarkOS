// SPDX-License-Identifier: MIT
/*
 * src/kernel/init.c - Idle init process for the userspace service loop
 */
#include "user/lib/libapi.h"
void main()
{
    while (1) {
        qk_sleep(1000);
    }
}
