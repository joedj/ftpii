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
#include <errno.h>
#include <fat.h>
#include <fst/fst.h>
#include <isfs/isfs.h>
#include <iso/iso.h>
#include <nandimg/nandimg.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>
#include <ogc/usbstorage.h>
#include <sdcard/gcsd.h>
#include <sdcard/wiisd_io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <wiiuse/wpad.h>
#include <wod/wod.h>

#include "common.h"

#define MAX_NET_BUFFER_SIZE 32768
#define MIN_NET_BUFFER_SIZE 4096
#define FREAD_BUFFER_SIZE 32768

VIRTUAL_PARTITION VIRTUAL_PARTITIONS[] = {
    { "SD Gecko A", "/gcsda", "gcsda", "gcsda:/", false, false, &__io_gcsda },
    { "SD Gecko B", "/gcsdb", "gcsdb", "gcsdb:/", false, false, &__io_gcsdb },
    { "Front SD", "/sd", "sd", "sd:/", false, false, &__io_wiisd },
    { "USB storage device", "/usb", "usb", "usb:/", false, false, &__io_usbstorage },
    { "ISO9660 filesystem", "/dvd", "dvd", "dvd:/", false, false, NULL },
    { "Wii disc image", "/wod", "wod", "wod:/", false, false, NULL },
    { "Wii disc filesystem", "/fst", "fst", "fst:/", false, false, NULL },
    { "NAND images", "/nand", "nand", "nand:/", false, false, NULL },
    { "NAND filesystem", "/isfs", "isfs", "isfs:/", false, false, NULL }
};
const u32 MAX_VIRTUAL_PARTITIONS = (sizeof(VIRTUAL_PARTITIONS) / sizeof(VIRTUAL_PARTITION));

VIRTUAL_PARTITION *PA_GCSDA = VIRTUAL_PARTITIONS + 0;
VIRTUAL_PARTITION *PA_GCSDB = VIRTUAL_PARTITIONS + 1;
VIRTUAL_PARTITION *PA_SD    = VIRTUAL_PARTITIONS + 2;
VIRTUAL_PARTITION *PA_USB   = VIRTUAL_PARTITIONS + 3;
VIRTUAL_PARTITION *PA_DVD   = VIRTUAL_PARTITIONS + 4;
VIRTUAL_PARTITION *PA_WOD   = VIRTUAL_PARTITIONS + 5;
VIRTUAL_PARTITION *PA_FST   = VIRTUAL_PARTITIONS + 6;
VIRTUAL_PARTITION *PA_NAND  = VIRTUAL_PARTITIONS + 7;
VIRTUAL_PARTITION *PA_ISFS  = VIRTUAL_PARTITIONS + 8;

static const u32 CACHE_PAGES = 8;
static u32 NET_BUFFER_SIZE = MAX_NET_BUFFER_SIZE;

static bool fatInitState = false;
static bool _dvd_mountWait = false;
static u64 dvd_last_stopped = 0;
static u32 device_check_iteration = 0;

bool hbc_stub() {
    return !!*(u32 *)0x80001800;
}

void die(char *msg) {
    perror(msg);
    sleep(5);
    if (hbc_stub()) {
        exit(1);
    } else {
        SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    }
}

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

const char *to_real_prefix(VIRTUAL_PARTITION *partition) {
    return partition->prefix;
}

static VIRTUAL_PARTITION *to_virtual_partition(const char *virtual_prefix) {
    u32 i;
    for (i = 0; i < MAX_VIRTUAL_PARTITIONS; i++)
        if (!strcasecmp(VIRTUAL_PARTITIONS[i].alias, virtual_prefix))
            return &VIRTUAL_PARTITIONS[i];
    return NULL;
}

static bool is_gecko(VIRTUAL_PARTITION *partition) {
    return partition == PA_GCSDA || partition == PA_GCSDB;
}

static bool is_fat(VIRTUAL_PARTITION *partition) {
    return partition == PA_SD || partition == PA_USB || is_gecko(partition);
}

static bool is_dvd(VIRTUAL_PARTITION *partition) {
    return partition == PA_DVD || partition == PA_WOD || partition == PA_FST;
}

bool mounted(VIRTUAL_PARTITION *partition) {
    DIR_ITER *dir = diropen(to_real_prefix(partition));
    if (dir) {
        dirclose(dir);
        return true;
    }
    return false;
}

static bool was_inserted_or_removed(VIRTUAL_PARTITION *partition) {
    if ((!partition->disc || partition->geckofail) && !is_dvd(partition)) return false;
    bool already_inserted = partition->inserted || mounted(partition);
    if (!already_inserted && partition == PA_SD) partition->disc->startup();
    if (is_dvd(partition)) {
        if (!dvd_mountWait()) {
            u32 status;
            if (!DI_GetCoverRegister(&status)) partition->inserted = (status & 2) == 2;
        }
    } else {
        partition->inserted = partition->disc->isInserted();        
    }
    return already_inserted != partition->inserted;
}

bool dvd_mountWait() {
    return _dvd_mountWait;
}

void set_dvd_mountWait(bool state) {
    _dvd_mountWait = state;
}

static u64 dvd_last_access() {
    return MAX(MAX(ISO9660_LastAccess(), WOD_LastAccess()), FST_LastAccess());
}

s32 dvd_stop() {
    dvd_last_stopped = gettime();
    return DI_StopMotor();
}

static void fat_enable_readahead(VIRTUAL_PARTITION *partition) {
    // if (!fatEnableReadAhead(to_real_prefix(partition), 64, 128))
    //     printf("Could not enable FAT read-ahead caching on %s, speed will suffer...\n", partition->name);
}

static void fat_enable_readahead_all() {
    u32 i;
    for (i = 0; i < MAX_VIRTUAL_PARTITIONS; i++) {
        VIRTUAL_PARTITION *partition = VIRTUAL_PARTITIONS + i;
        if (is_fat(partition) && mounted(partition)) fat_enable_readahead(partition);
    }
}

bool initialise_fat() {
    if (fatInitState) return true;
    if (fatInit(CACHE_PAGES, false)) { 
        fatInitState = 1;
        fat_enable_readahead_all();
    }
    return fatInitState;
}

typedef enum { MOUNTSTATE_START, MOUNTSTATE_SELECTDEVICE, MOUNTSTATE_WAITFORDEVICE } mountstate_t;
static mountstate_t mountstate = MOUNTSTATE_START;
static VIRTUAL_PARTITION *mount_partition = NULL;
static u64 mount_timer = 0;

bool mount(VIRTUAL_PARTITION *partition) {
    if (!partition || mounted(partition)) return false;
    
    bool success = false;
    printf("Mounting %s...", partition->name);
    if (is_dvd(partition)) {
        set_dvd_mountWait(true);
        DI_Mount();
        u64 timeout = gettime() + secs_to_ticks(10);
        while (!(DI_GetStatus() & DVD_READY) && gettime() < timeout) usleep(2000);
        if (DI_GetStatus() & DVD_READY) {
            set_dvd_mountWait(false);
            if (partition == PA_DVD) success = ISO9660_Mount();
            else if (partition == PA_WOD) success = WOD_Mount();
            else if (partition == PA_FST) success = FST_Mount();
        }
        if (!dvd_mountWait() && !dvd_last_access()) dvd_stop();
    } else if (is_fat(partition)) {
        bool retry_gecko = true;
        gecko_retry:
        if (partition->disc->shutdown() & partition->disc->startup()) {
            if (!fatInitState) {
                if (initialise_fat()) success = mounted(partition);
            } else if (fatMount(partition->mount_point, partition->disc, 0, CACHE_PAGES)) {
                fat_enable_readahead(partition);
                success = true;
            }
        } else if (is_gecko(partition) && retry_gecko) {
            retry_gecko = false;
            sleep(1);
            goto gecko_retry;
        }
    } else if (partition == PA_NAND) {
        success = NANDIMG_Mount();
    } else if (partition == PA_ISFS) {
        success = ISFS_Mount();
    }
    printf(success ? "succeeded.\n" : "failed.\n");
    if (success && is_gecko(partition)) partition->geckofail = false;

    return success;
}

bool mount_virtual(const char *dir) {
    return mount(to_virtual_partition(dir));
}

bool unmount(VIRTUAL_PARTITION *partition) {
    if (!partition || !mounted(partition)) return false;

    printf("Unmounting %s...", partition->name);
    bool success = false;
    if (is_dvd(partition)) {
        if (partition == PA_DVD) success = ISO9660_Unmount();
        else if (partition == PA_WOD) success = WOD_Unmount();
        else if (partition == PA_FST) success = FST_Unmount();
        if (!dvd_mountWait() && !dvd_last_access()) dvd_stop();
    } else if (is_fat(partition)) {
        fatUnmount(to_real_prefix(partition));
        success = true;
    } else if (partition == PA_NAND) {
        success = NANDIMG_Unmount();
    } else if (partition == PA_ISFS) {
        success = ISFS_Unmount();
    }
    printf(success ? "succeeded.\n" : "failed.\n");

    return success;
}

bool unmount_virtual(const char *dir) {
    return unmount(to_virtual_partition(dir));
}

void check_removable_devices() {
    if (device_check_iteration++ % 200) return;

    u32 i;
    for (i = 0; i < MAX_VIRTUAL_PARTITIONS; i++) {
        VIRTUAL_PARTITION *partition = VIRTUAL_PARTITIONS + i;
        if (mount_timer && partition == mount_partition) continue;
        if (was_inserted_or_removed(partition)) {
            if (partition->inserted && (partition == PA_DVD || (!is_dvd(partition) && !mounted(partition)))) {
                printf("Device inserted; ");
                if (partition == PA_DVD) {
                    set_dvd_mountWait(true);
                    DI_Mount();
                    printf("Mounting DVD...\n");
                } else if (!mount(partition) && is_gecko(partition)) {
                    printf("%s failed to automount.  Insertion or removal will not be detected until it is mounted manually.\n", partition->name);
                    printf("Note that inserting an SD Gecko without an SD card in it can be problematic.\n");
                    partition->geckofail = true;
                }
            } else if (!partition->inserted && mounted(partition)) {
                printf("Device removed; ");
                unmount(partition);
            }
        }
    }
}

static void dvd_unmount() {
    unmount(PA_WOD);
    unmount(PA_FST);
    unmount(PA_DVD);
    dvd_stop();
}

s32 dvd_eject() {
    dvd_unmount();
    return DI_Eject();
}

static volatile u8 _reset = 0;
static volatile u8 _power = 0;

u8 reset() {
    return _reset;
}

u8 power() {
    return _power;
}

void set_reset_flag() {
    _reset = 1;
}

static void set_power_flag() {
    set_reset_flag();
    _power = 1;
}

void initialise_reset_buttons() {
    SYS_SetResetCallback(set_reset_flag);
    SYS_SetPowerCallback(set_power_flag);
    WPAD_SetPowerButtonCallback(set_power_flag);
}

void process_remount_event() {
    if (mountstate == MOUNTSTATE_START || mountstate == MOUNTSTATE_SELECTDEVICE) {
        mountstate = MOUNTSTATE_SELECTDEVICE;
        mount_partition = NULL;
        printf("\nWhich device would you like to remount? (hold button on controller #1)\n\n");
        printf("           SD Gecko A (Up)\n");
        printf("                  | \n");
        printf("Front SD (Left) --+-- USB Storage Device (Right)\n");
        printf("                  | \n");
        printf("           SD Gecko B (Down)\n");
        printf("                  | \n");
        printf("              DVD (1/X)\n");
    } else if (mountstate == MOUNTSTATE_WAITFORDEVICE) {
        mount_timer = 0;
        mountstate = MOUNTSTATE_START;
        if (is_dvd(mount_partition)) {
            set_dvd_mountWait(true);
            DI_Mount();
            printf("Mounting DVD...\n");
        } else {
            mount(mount_partition);
        }
        mount_partition = NULL;
    }
}

void process_device_select_event(u32 pressed) {
    if (mountstate == MOUNTSTATE_SELECTDEVICE) {
        if (pressed & WPAD_BUTTON_LEFT) mount_partition = PA_SD;
        else if (pressed & WPAD_BUTTON_RIGHT) mount_partition = PA_USB;
        else if (pressed & WPAD_BUTTON_UP) mount_partition = PA_GCSDA;
        else if (pressed & WPAD_BUTTON_DOWN) mount_partition = PA_GCSDB;
        else if (pressed & WPAD_BUTTON_1) mount_partition = PA_DVD;
        if (mount_partition) {
            mountstate = MOUNTSTATE_WAITFORDEVICE;
            if (is_dvd(mount_partition)) dvd_unmount();
            else if (is_fat(mount_partition)) unmount(mount_partition);
            printf("To continue after changing the device hold B on controller #1 or wait 30 seconds.\n");
            mount_timer = gettime() + secs_to_ticks(30);
        }
    }
}

#define DVD_MOTOR_TIMEOUT 300

void process_timer_events() {
    u64 now = gettime();
    u64 dvd_access = dvd_last_access();
    if (dvd_access > dvd_last_stopped && now > (dvd_access + secs_to_ticks(DVD_MOTOR_TIMEOUT)) && !dvd_mountWait()) {
        printf("Stopping DVD drive motor after %u seconds of inactivity.\n", DVD_MOTOR_TIMEOUT);
        dvd_unmount();
    }
    if (mount_timer && now > mount_timer) process_remount_event();
}

void initialise_video() {
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

static bool check_reset_synchronous() {
    return _reset || check_wiimote(WPAD_BUTTON_A) || check_gamecube(PAD_BUTTON_A);
}

void initialise_network() {
    printf("Waiting for network to initialise...\n");
    s32 result = -1;
    while (!check_reset_synchronous() && result < 0) {
        while (!check_reset_synchronous() && (result = net_init()) == -EAGAIN);
        if (result < 0) printf("net_init() failed: [%i] %s, retrying...\n", result, strerror(-result));
    }
    if (result >= 0) {
        u32 ip = 0;
        do {
            ip = net_gethostip();
            if (!ip) printf("net_gethostip() failed, retrying...\n");
        } while (!check_reset_synchronous() && !ip);
        if (ip) printf("Network initialised.  Wii IP address: %s\n", inet_ntoa(*(struct in_addr *)&ip));
    }
}

s32 set_blocking(s32 s, bool blocking) {
    s32 flags;
    if ((flags = net_fcntl(s, F_GETFL, 0)) < 0) {
        printf("DEBUG: set_blocking(%i, %i): Unable to get flags: [%i] %s\n", s, blocking, -flags, strerror(-flags));
    } else if ((flags = net_fcntl(s, F_SETFL, blocking ? (flags&~4) : (flags|4))) < 0) {
        printf("DEBUG: set_blocking(%i, %i): Unable to set flags: [%i] %s\n", s, blocking, -flags, strerror(-flags));
    }
    return flags;
}

s32 net_close_blocking(s32 s) {
    set_blocking(s, true);
    return net_close(s);
}

s32 create_server(u16 port) {
    s32 server = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) die("Error creating socket, exiting");
    set_blocking(server, false);

    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(port);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    if (net_bind(server, (struct sockaddr *)&bindAddress, sizeof(bindAddress)) < 0) {
        net_close(server);
        die("Error binding socket");
    }
    if (net_listen(server, 3) < 0) {
        net_close(server);
        die("Error listening on socket");
    }

    return server;
}

typedef s32 (*transferrer_type)(s32 s, void *mem, s32 len);
static s32 transfer_exact(s32 s, char *buf, s32 length, transferrer_type transferrer) {
    s32 result = 0;
    s32 remaining = length;
    s32 bytes_transferred;
    set_blocking(s, true);
    while (remaining) {
        try_again_with_smaller_buffer:
        bytes_transferred = transferrer(s, buf, MIN(remaining, NET_BUFFER_SIZE));
        if (bytes_transferred > 0) {
            remaining -= bytes_transferred;
            buf += bytes_transferred;
        } else if (bytes_transferred < 0) {
            if (bytes_transferred == -EINVAL && NET_BUFFER_SIZE == MAX_NET_BUFFER_SIZE) {
                NET_BUFFER_SIZE = MIN_NET_BUFFER_SIZE;
                goto try_again_with_smaller_buffer;
            }
            result = bytes_transferred;
            break;
        } else {
            result = -ENODATA;
            break;
        }
    }
    set_blocking(s, false);
    return result;
}

s32 send_exact(s32 s, char *buf, s32 length) {
    return transfer_exact(s, buf, length, (transferrer_type)net_write);
}

s32 send_from_file(s32 s, FILE *f) {
    char buf[FREAD_BUFFER_SIZE];
    s32 bytes_read;
    s32 result = 0;

    bytes_read = fread(buf, 1, FREAD_BUFFER_SIZE, f);
    if (bytes_read > 0) {
        result = send_exact(s, buf, bytes_read);
        if (result < 0) {
            // printf("DEBUG: send_from_file() net_write error: [%i] %s\n", -result, strerror(-result));
            goto end;
        }
    }
    if (bytes_read < FREAD_BUFFER_SIZE) {
        result = -!feof(f);
        // if (result < 0) {
        //     printf("DEBUG: send_from_file() fread error: [%i] %s\n", ferror(f), strerror(ferror(f)));
        // }
        goto end;
    }
    return -EAGAIN;
    end:
    return result;
}

s32 recv_to_file(s32 s, FILE *f) {
    char buf[NET_BUFFER_SIZE];
    s32 bytes_read;
    while (1) {
        try_again_with_smaller_buffer:
        bytes_read = net_read(s, buf, NET_BUFFER_SIZE);
        if (bytes_read < 0) {
            if (bytes_read == -EINVAL && NET_BUFFER_SIZE == MAX_NET_BUFFER_SIZE) {
                NET_BUFFER_SIZE = MIN_NET_BUFFER_SIZE;
                goto try_again_with_smaller_buffer;
            }
            // if (bytes_read != -EAGAIN) {
            //     printf("DEBUG: recv_to_file() net_read error: [%i] %s\n", -bytes_read, strerror(-bytes_read));
            // }
            return bytes_read;
        } else if (bytes_read == 0) {
            return 0;
        }

        s32 bytes_written = fwrite(buf, 1, bytes_read, f);
        if (bytes_written < bytes_read) {
            // printf("DEBUG: recv_to_file() fwrite error: [%i] %s\n", ferror(f), strerror(ferror(f)));
            return -1;
        }
    }
}

/*
    result must be able to hold up to maxsplit+1 null-terminated strings of length strlen(s)
    returns the number of strings stored in the result array (up to maxsplit+1)
*/
u32 split(char *s, char sep, u32 maxsplit, char *result[]) {
    u32 num_results = 0;
    u32 result_pos = 0;
    u32 trim_pos = 0;
    bool in_word = false;
    for (; *s; s++) {
        if (*s == sep) {
            if (num_results <= maxsplit) {
                in_word = false;
                continue;
            } else if (!trim_pos) {
                trim_pos = result_pos;
            }
        } else if (trim_pos) {
            trim_pos = 0;
        }
        if (!in_word) {
            in_word = true;
            if (num_results <= maxsplit) {
                num_results++;
                result_pos = 0;
            }
        }
        result[num_results - 1][result_pos++] = *s;
        result[num_results - 1][result_pos] = '\0';
    }
    if (trim_pos) {
        result[num_results - 1][trim_pos] = '\0';
    }
    u32 i = num_results;
    for (i = num_results; i <= maxsplit; i++) {
        result[i][0] = '\0';
    }
    return num_results;
}

/*
    Returns a copy of path up to the last '/' character,
    If path does not contain '/', returns "".
    Returns a pointer to internal static storage space that will be overwritten by subsequent calls.
    This function is not thread-safe.
*/
char *dirname(char *path) {
    static char result[MAXPATHLEN];
    strncpy(result, path, MAXPATHLEN - 1);
    result[MAXPATHLEN - 1] = '\0';
    s32 i;
    for (i = strlen(result) - 1; i >= 0; i--) {
        if (result[i] == '/') {
            result[i] = '\0';
            return result;
        }
    }
    return "";
}

/*
    Returns a pointer into path, starting after the right-most '/' character.
    If path does not contain '/', returns path.
*/
char *basename(char *path) {
    s32 i;
    for (i = strlen(path) - 1; i >= 0; i--) {
        if (path[i] == '/') {
            return path + i + 1;
        }
    }
    return path;
}
