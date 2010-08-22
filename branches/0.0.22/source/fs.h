// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
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

VIRTUAL_PARTITION VIRTUAL_PARTITIONS[11];
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
VIRTUAL_PARTITION *PA_SEEPROM;

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
