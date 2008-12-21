/*

libfst -- a Wii disc filesystem devoptab library for the Wii

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
#include <fst/fst.h>
#include <ogcsys.h>
#include <stdio.h>
#include <string.h>
#include <sys/dir.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

static void initialise_video() {
    VIDEO_Init();
    GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
    CON_InitEx(rmode, 20, 30, rmode->fbWidth - 40, rmode->xfbHeight - 60);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
}

static bool initialise() {
    printf("Initialising.  This will wait for a disc to be inserted...\n");
    DI_Mount();
    while (DI_GetStatus() & DVD_INIT) usleep(5000);
    if (DI_GetStatus() & DVD_READY) return FST_Mount();
    return false;
}

int main(int argc, char **argv) {
    DI_Init();
    initialise_video();
    PAD_Init();
    WPAD_Init();

    if (!initialise()) {
        printf("Unable to initialise. DI_GetStatus() == %i\n", DI_GetStatus());
        return 1;
    }

    printf("Listing fst:/\n");
    u32 fileCount = 0;
    DIR_ITER *dir = diropen("fst:/");
    if (!dir) {
        printf("Unable to diropen(\"fst:/\"): %i\n", errno);
        return 1;
    }
    char filename[FST_MAXPATHLEN];
    struct stat st;
    while (dirnext(dir, filename, &st) == 0) {
        printf("%10llu %s\n", st.st_size, filename);
        fileCount++;
    }
    printf("%u entries.\n", fileCount);

    FST_Unmount();
    DI_Close();

    printf("Exiting in 5 seconds...\n");
    sleep(5);

    if (!*(u32 *)0x80001800) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    return 0;
}
