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
#include <di/di.h>
#include <fst/fst.h>
#include <gccore.h>
#include <iso/iso.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <wod/wod.h>

#include "dvd.h"
#include "fs.h"

#define DVD_MOTOR_TIMEOUT 300

static bool _dvd_mountWait = false;
static u64 dvd_last_stopped = 0;

bool dvd_mountWait() {
    return _dvd_mountWait;
}

void set_dvd_mountWait(bool state) {
    _dvd_mountWait = state;
}

u64 dvd_last_access() {
    return MAX(MAX(ISO9660_LastAccess(), WOD_LastAccess()), FST_LastAccess());
}

s32 dvd_stop() {
    dvd_last_stopped = gettime();
    return DI_StopMotor();
}

void dvd_unmount() {
    unmount(PA_WOD);
    unmount(PA_FST);
    unmount(PA_DVD);
    dvd_stop();
}

s32 dvd_eject() {
    dvd_unmount();
    return DI_Eject();
}

void check_dvd_motor_timeout(u64 now) {
    u64 dvd_access = dvd_last_access();
    if (dvd_access > dvd_last_stopped && now > (dvd_access + secs_to_ticks(DVD_MOTOR_TIMEOUT)) && !dvd_mountWait()) {
        printf("Stopping DVD drive motor after %u seconds of inactivity.\n", DVD_MOTOR_TIMEOUT);
        dvd_unmount();
    }
}

void check_dvd_mount() {
    if (dvd_mountWait() && DI_GetStatus() & DVD_READY) {
        set_dvd_mountWait(false);
        bool wod = false, fst = false, iso = false;
        printf("Mounting %s...", PA_WOD->name);
        printf((wod = WOD_Mount()) ? "succeeded.\n" : "failed.\n");
        printf("Mounting %s...", PA_FST->name);
        printf((fst = FST_Mount()) ? "succeeded.\n" : "failed.\n");
        printf("Mounting %s...", PA_DVD->name);
        printf((iso = ISO9660_Mount()) ? "succeeded.\n" : "failed.\n");
        if (!(wod || fst || iso)) dvd_stop();
    }
}
