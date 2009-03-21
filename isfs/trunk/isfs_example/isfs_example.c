/*

libisfs -- a NAND filesystem devoptab library for the Wii

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
#include <isfs/isfs.h>
#include <ogcsys.h>
#include <stdio.h>
#include <string.h>
#include <sys/dir.h>
#include <unistd.h>

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

static bool initialise() {
    printf("Initialising...\n");
    if (ISFS_Initialize() == IPC_OK) return ISFS_Mount();
    else return false;
}

int main(int argc, char **argv) {
    initialise_video();
    PAD_Init();

    if (!initialise()) {
        printf("Unable to initialise.\n");
        return 1;
    }

    printf("Listing isfs:/\n");
    u32 fileCount = 0;
    DIR_ITER *dir = diropen("isfs:/");
    if (!dir) {
        printf("Unable to diropen(\"isfs:/\"): %i\n", errno);
        return 1;
    }
    char filename[ISFS_MAXPATHLEN];
    struct stat st;
    while (dirnext(dir, filename, &st) == 0) {
        printf("%10llu %s\n", st.st_size, filename);
        fileCount++;
    }
    printf("%u entries.\n", fileCount);

    ISFS_Unmount();

    printf("Exiting in 5 seconds...\n");
    sleep(5);

    if (!*(u32 *)0x80001800) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    return 0;
}
