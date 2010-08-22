// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiiuse/wpad.h>

#include "pad.h"
#include "reset.h"

static volatile bool _reset = false;
static volatile bool _power = false;

u8 reset() {
    return _reset;
}

void set_reset_flag() {
    _reset = true;
}

static void set_power_flag() {
    _reset = true;
    _power = true;
}

void initialise_reset_buttons() {
    SYS_SetResetCallback(set_reset_flag);
    SYS_SetPowerCallback(set_power_flag);
    WPAD_SetPowerButtonCallback(set_power_flag);
}

bool check_reset_synchronous() {
    return _reset || check_wiimote(WPAD_BUTTON_A) || check_gamecube(PAD_BUTTON_A);
}

void maybe_poweroff() {
    if (_power) SYS_ResetSystem(SYS_POWEROFF, 0, 0);
}

void die(char *msg, int errnum) {
    printf("%s: [%i] %s\n", msg, errnum, strerror(errnum));
    printf("Program halted.  Press reset to exit.\n");
    while (!check_reset_synchronous()) VIDEO_WaitVSync();
    maybe_poweroff();
    exit(1);
}
