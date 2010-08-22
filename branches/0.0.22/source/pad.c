// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <wiiuse/wpad.h>

#include "pad.h"

u32 check_wiimote(u32 mask) {
    WPAD_ScanPads();
    u32 pressed = WPAD_ButtonsDown(0);
    if (pressed & mask) return pressed;
    return 0;
}

u32 check_gamecube(u32 mask) {
    PAD_ScanPads();
    u32 pressed = PAD_ButtonsDown(0);
    if (pressed & mask) {
        VIDEO_WaitVSync();
        return pressed;
    }
    return 0;
}
