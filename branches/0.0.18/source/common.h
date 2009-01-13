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
#ifndef _COMMON_H_
#define _COMMON_H_

#include <fat.h>
#include <network.h>
#include <ogcsys.h>
#include <stdio.h>
#include <sys/dir.h>

typedef struct {
    const char *name;
    const char *alias;
    const char *mount_point;
    const char *prefix;
    bool inserted;
    bool geckofail;
    const DISC_INTERFACE *disc;
} VIRTUAL_PARTITION;

VIRTUAL_PARTITION VIRTUAL_PARTITIONS[9];
const u32 MAX_VIRTUAL_PARTITIONS;
VIRTUAL_PARTITION *PA_GCSDA;
VIRTUAL_PARTITION *PA_GCSDB;
VIRTUAL_PARTITION *PA_SD;
VIRTUAL_PARTITION *PA_USB;
VIRTUAL_PARTITION *PA_DVD;
VIRTUAL_PARTITION *PA_WOD;
VIRTUAL_PARTITION *PA_FST;
VIRTUAL_PARTITION *PA_NAND;
VIRTUAL_PARTITION *PA_ISFS;

bool initialise_fat();

u8 reset();

u8 power();

void set_reset_flag();

void initialise_reset_buttons();

bool hbc_stub();

void die(char *msg);

u32 check_wiimote(u32 mask);

u32 check_gamecube(u32 mask);

void initialise_video();

const char *to_real_prefix(VIRTUAL_PARTITION *partition);

bool mounted(VIRTUAL_PARTITION *partition);

bool mount(VIRTUAL_PARTITION *partition);

bool unmount(VIRTUAL_PARTITION *partition);

bool mount_virtual(const char *dir);

bool unmount_virtual(const char *dir);

void check_removable_devices();

bool dvd_mountWait();

void set_dvd_mountWait(bool state);

s32 dvd_stop();

s32 dvd_eject();

void process_remount_event();

void process_device_select_event(u32 pressed);

void process_timer_events();

void initialise_network();

s32 set_blocking(s32 s, bool blocking);

s32 net_close_blocking(s32 s);

s32 create_server(u16 port);

s32 send_exact(s32 s, char *buf, s32 length);

s32 send_from_file(s32 s, FILE *f);

s32 recv_to_file(s32 s, FILE *f);

u32 split(char *s, char sep, u32 maxsplit, char *result[]);

char *dirname(char *path);
char *basename(char *path);

#endif /* _COMMON_H_ */