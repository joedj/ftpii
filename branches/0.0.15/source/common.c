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
#include <errno.h>
#include <fat.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

#include "common.h"

#define NET_BUFFER_SIZE 32768
#define FREAD_BUFFER_SIZE 32768

const char *VIRTUAL_PARTITION_ALIASES[] = { "/gc1", "/gc2", "/sd", "/usb" };
const u32 MAX_VIRTUAL_PARTITION_ALIASES = (sizeof(VIRTUAL_PARTITION_ALIASES) / sizeof(char *));

static const u32 CACHE_PAGES = 8192;

static volatile bool fatInitState = false;

bool hbc_stub() {
    return !!*(u32*)0x80001800;
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

bool mounted(PARTITION_INTERFACE partition) {
    char prefix[] = "fatX:/";
    prefix[3] = partition + '0';
    DIR_ITER *dir = diropen(prefix);
    if (dir) {
        dirclose(dir);
        return true;
    }
    return false;
}

static void fat_enable_readahead(PARTITION_INTERFACE partition) {
    if (!fatEnableReadAhead(partition, 64, 128))
        printf("Could not enable FAT read-ahead caching on %s, speed will suffer...\n", VIRTUAL_PARTITION_ALIASES[partition - 1]);
}

static void fat_enable_readahead_all() {
    PARTITION_INTERFACE i;
    for (i = 1; i <= MAX_VIRTUAL_PARTITION_ALIASES; i++) {
        if (mounted(i)) fat_enable_readahead(i);
    }
}

bool initialise_fat() {
    if (!fatInitState && !fatInit(CACHE_PAGES, false)) { 
        printf("Unable to initialise FAT subsystem.  Are there any connected devices?\n"); 
    } else {
        fatInitState = 1;
        fat_enable_readahead_all();
    }
    return fatInitState;
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
}

typedef enum { MOUNTSTATE_START, MOUNTSTATE_SELECTDEVICE, MOUNTSTATE_WAITFORDEVICE } mountstate_t;

static mountstate_t mountstate = MOUNTSTATE_START;
static PARTITION_INTERFACE mount_partition;
static char *mount_deviceName = NULL;
static u64 mount_timer = 0;

void process_remount_event() {
    if (mountstate == MOUNTSTATE_START || mountstate == MOUNTSTATE_SELECTDEVICE) {
        mountstate = MOUNTSTATE_SELECTDEVICE;
        printf("\nWhich device would you like to remount? (hold button on controller #1)\n\n");
        printf("             SD Gecko A (Up)\n");
        printf("                  | \n");
        printf("Front SD (Left) --+-- USB Storage Device (Right)\n");
        printf("                  |\n");
        printf("             SD Gecko B (Down)\n");
    } else if (mountstate == MOUNTSTATE_WAITFORDEVICE) {
        mount_timer = 0;
        mountstate = MOUNTSTATE_START;
        bool success = false;
        if (!fatInitState) {
            if (!initialise_fat()) {
                printf("Unable to initialise FAT subsystem, unable to mount %s\n", mount_deviceName);
                return;
            }
            if (mounted(mount_partition)) success = true;
        } else if (fatMountNormalInterface(mount_partition, CACHE_PAGES)) {
            success = true;
            fat_enable_readahead(mount_partition);
        }

        if (success) {
            printf("Success: %s is mounted.\n", mount_deviceName);
        } else {
            printf("Error mounting %s.\n", mount_deviceName);
        }
    }
}

void process_device_select_event(u32 pressed) {
    if (mountstate == MOUNTSTATE_SELECTDEVICE) {
        mount_deviceName = NULL;
        if (pressed & WPAD_BUTTON_LEFT) {
            mount_partition = PI_INTERNAL_SD;
            mount_deviceName = "Front SD";
        } else if (pressed & WPAD_BUTTON_RIGHT) {
            mount_partition = PI_USBSTORAGE;
            mount_deviceName = "USB storage";
        } else if (pressed & WPAD_BUTTON_UP) {
            mount_partition = PI_SDGECKO_A;
            mount_deviceName = "SD Gecko in slot A";
        } else if (pressed & WPAD_BUTTON_DOWN) {
            mount_partition = PI_SDGECKO_B;
            mount_deviceName = "SD Gecko in slot B";
        }
        if (mount_deviceName) {
            mountstate = MOUNTSTATE_WAITFORDEVICE;
            printf("Unmounting %s ...", mount_deviceName);
            fflush(stdout);
            if (!fatUnmount(mount_partition)) {
                // TODO: try unsafe unmount stuff
                printf("failure\n");
            } else {
                printf("done\n");
            }
            printf("To continue after changing the %s hold B on controller #1 or wait 30 seconds.\n", mount_deviceName);
            mount_timer = gettime() + secs_to_ticks(30);
        }
    }
}

void process_timer_events() {
    u64 now = gettime();
    if (mount_timer && now > mount_timer) process_remount_event();
}

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

void initialise_video() {
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    printf("\x1b[2;0H");
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
    set_blocking(s, true);
    while (remaining) {
        s32 bytes_transferred = transferrer(s, buf, MIN(remaining, NET_BUFFER_SIZE));
        if (bytes_transferred > 0) {
            remaining -= bytes_transferred;
            buf += bytes_transferred;
        } else if (bytes_transferred < 0) {
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
            printf("DEBUG: send_from_file() net_write error: [%i] %s\n", -result, strerror(-result));
            goto end;
        }
    }
    if (bytes_read < FREAD_BUFFER_SIZE) {
        result = -!feof(f);
        if (result < 0) {
            printf("DEBUG: send_from_file() fread error: [%i] %s\n", ferror(f), strerror(ferror(f)));
        }
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
        bytes_read = net_read(s, buf, NET_BUFFER_SIZE);
        if (bytes_read < 0) {
            if (bytes_read != -EAGAIN) {
                printf("DEBUG: recv_to_file() net_read error: [%i] %s\n", -bytes_read, strerror(-bytes_read));
            }
            return bytes_read;
        } else if (bytes_read == 0) {
            return 0;
        }

        s32 bytes_written = fwrite(buf, 1, bytes_read, f);
        if (bytes_written < bytes_read) {
            printf("DEBUG: recv_to_file() fwrite error: [%i] %s\n", ferror(f), strerror(ferror(f)));
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