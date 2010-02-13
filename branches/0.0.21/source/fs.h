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
#ifndef _FS_H_
#define _FS_H_

#include <ogc/disc_io.h>

typedef struct {
    const char *name;
    const char *alias;
    const char *mount_point;
    const char *prefix;
    bool inserted;
    bool geckofail;
    const DISC_INTERFACE *disc;
} VIRTUAL_PARTITION;

VIRTUAL_PARTITION VIRTUAL_PARTITIONS[10];
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
VIRTUAL_PARTITION *PA_OTP;

void initialise_fs();

bool mounted(VIRTUAL_PARTITION *partition);

bool mount(VIRTUAL_PARTITION *partition);

bool unmount(VIRTUAL_PARTITION *partition);

bool mount_virtual(const char *dir);

bool unmount_virtual(const char *dir);

void check_removable_devices(u64 now);

void process_remount_event();

void process_device_select_event(u32 pressed);

void check_mount_timer(u64 now);

char *dirname(char *path);

char *basename(char *path);

#endif /* _FS_H_ */
