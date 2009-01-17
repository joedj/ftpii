/*

ftpii -- an FTP server for the Wii

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
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

#include "dvd.h"
#include "ftp.h"
#include "fs.h"
#include "net.h"
#include "pad.h"
#include "reset.h"

static const u16 PORT = 21;
static const char *APP_DIR_PREFIX = "ftpii_";

static void initialise_video() {
    VIDEO_Init();
    GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
    VIDEO_Configure(rmode);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    CON_InitEx(rmode, 20, 30, rmode->fbWidth - 40, rmode->xfbHeight - 60);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

static void initialise_ftpii() {
    DI_Init();
    initialise_video();
    PAD_Init();
    WPAD_Init();
    initialise_reset_buttons();
    printf("To exit, hold A on controller #1 or press the reset button.\n");
    initialise_network();
    initialise_fs();
    printf("To remount a device, hold B on controller #1.\n");
}

static void set_password_from_executable(char *executable) {
    char *dir = basename(dirname(executable));
    if (strncasecmp(APP_DIR_PREFIX, dir, strlen(APP_DIR_PREFIX)) == 0) {
        set_ftp_password(dir + strlen(APP_DIR_PREFIX));
    }
}

static void process_wiimote_events() {
    u32 pressed = check_wiimote(WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT | WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_1);
    if (pressed & WPAD_BUTTON_A) set_reset_flag();
    else if (pressed & WPAD_BUTTON_B) process_remount_event();
    else if (pressed & (WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT | WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_1)) process_device_select_event(pressed);
}

static void process_gamecube_events() {
    u32 pressed = check_gamecube(PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_X);
    if (pressed & PAD_BUTTON_A) set_reset_flag();
    else if (pressed & PAD_BUTTON_B) process_remount_event();
    else if (pressed & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_X)) {
        u32 wpad_pressed = 0;
        if (pressed & PAD_BUTTON_LEFT) wpad_pressed |= WPAD_BUTTON_LEFT;
        if (pressed & PAD_BUTTON_RIGHT) wpad_pressed |= WPAD_BUTTON_RIGHT;
        if (pressed & PAD_BUTTON_UP) wpad_pressed |= WPAD_BUTTON_UP;
        if (pressed & PAD_BUTTON_DOWN) wpad_pressed |= WPAD_BUTTON_DOWN;
        if (pressed & PAD_BUTTON_X) wpad_pressed |= WPAD_BUTTON_1;
        process_device_select_event(wpad_pressed);
    }
}

static void process_timer_events() {
    u64 now = gettime();
    check_dvd_motor_timeout(now);
    check_mount_timer(now);
}

int main(int argc, char **argv) {
    initialise_ftpii();

    if (argc > 1) {
        set_ftp_password(argv[1]);
    } else if (argc == 1) {
        set_password_from_executable(argv[0]);
    }

    s32 server = create_server(PORT);
    printf("Listening on TCP port %u...\n", PORT);
    while (!reset()) {
        check_removable_devices();
        check_dvd_mount();
        process_ftp_events(server);
        process_wiimote_events();
        process_gamecube_events();
        process_timer_events();
        usleep(5000);
    }
    cleanup_ftp();
    net_close(server);

    u32 i;
    for (i = 0; i < MAX_VIRTUAL_PARTITIONS; i++) unmount(VIRTUAL_PARTITIONS + i);

    printf("\nKTHXBYE\n");

    if (dvd_mountWait()) printf("NOTE: Due to a known bug in libdi, ftpii is unable to exit until a DVD is inserted.\n");
    dvd_stop();
    DI_Close();
    ISFS_Deinitialize();

    poweroff_or_sysmenu();
    return 0;
}
