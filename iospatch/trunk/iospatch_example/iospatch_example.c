// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <di/di.h>
#include <errno.h>
#include <iospatch/iospatch.h>
#include <isfs/isfs.h>
#include <ogcsys.h>
#include <stdio.h>
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
    CON_EnableGecko(1, 0);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

static bool initialise() {
    printf("Initialising...\n");
    if (!IOSPATCH_Apply()) return false;
    if (ISFS_Initialize() == IPC_OK) return ISFS_Mount();
    else return false;
}

int main(int argc, char **argv) {
    DI_Init();
    initialise_video();
    PAD_Init();

    if (!initialise()) {
        printf("Unable to initialise.\n");
        return 1;
    }

    printf("Listing isfs:/title/00000001/00000002/data\n");
    u32 fileCount = 0;
    DIR_ITER *dir = diropen("isfs:/title/00000001/00000002/data");
    if (!dir) {
        printf("Unable to diropen(\"isfs:/title/00000001/00000002/data\"): %i\n", errno);
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

    return 0;
}
