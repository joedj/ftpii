// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <di/di.h>
#include <errno.h>
#include <iso/iso.h>
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
    CON_EnableGecko(1, 0);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

static bool initialise_dvd() {
    printf("Initialising DVD.  This will wait for a disc to be inserted...\n");
    DI_Mount();
    while (DI_GetStatus() & DVD_INIT) usleep(5000);
    if (DI_GetStatus() & DVD_READY) return ISO9660_Mount();
    return false;
}

int main(int argc, char **argv) {
    DI_Init();
    initialise_video();
    PAD_Init();

    if (!initialise_dvd()) {
        printf("Unable to initialise DVD. DI_GetStatus() == %i\n", DI_GetStatus());
        return 1;
    }

    printf("Listing DVD root directory:\n");
    u32 fileCount = 0;
    DIR_ITER *dir = diropen("dvd:/");
    if (!dir) {
        printf("Unable to diropen(\"dvd:/\"): %i\n", errno);
        return 1;
    }
    char filename[ISO_MAXPATHLEN + 1];
    struct stat st;
    while (dirnext(dir, filename, &st) == 0) {
        if (st.st_mode & S_IFDIR) strcat(filename, "/");
        printf("%10llu %s\n", st.st_size, filename);
        fileCount++;
    }
    printf("%u entries.\n", fileCount);

    ISO9660_Unmount();
    DI_Close();

    printf("Exiting in 5 seconds...\n");
    sleep(5);

    return 0;
}
