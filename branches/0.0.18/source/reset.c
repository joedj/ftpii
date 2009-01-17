/*

Copyright (C) 2008 Joseph Jordan <joe.ftpii@psychlaw.com.au>

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1.The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software in a
product, an acknowledgment in the product documentation would be
appreciated but is not required.

2.Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3.This notice may not be removed or altered from any source distribution.

*/
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

static bool exit_stub() {
    return !!*(u32 *)0x80001800;
}

void poweroff_or_sysmenu() {
    if (_power) SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    else if (!exit_stub()) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

void die(char *msg, int errnum) {
    printf("%s: [%i] %s\n", msg, errnum, strerror(errnum));
    printf("Program halted.  Press reset to exit.\n");
    while (!check_reset_synchronous()) VIDEO_WaitVSync();
    poweroff_or_sysmenu();
    exit(1);
}
