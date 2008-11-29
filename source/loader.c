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
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>

#include "loader.h"

extern void __exception_closeall();

static bool read_from_file(u8 *buf, FILE *f) {
    while (1) {
        s32 bytes_read = fread(buf, 1, 0x8000, f);
        if (bytes_read > 0) buf += bytes_read;
        if (bytes_read < 0x8000) return feof(f);
    }
}

typedef struct {
    u32 text_pos[7];
    u32 data_pos[11];
    u32 text_start[7];
    u32 data_start[11];
    u32 text_size[7];
    u32 data_size[11];
    u32 bss_start;
    u32 bss_size;
    u32 entry_point;
} dolheader;

u32 load_dol_image(void *dolstart) {
    dolheader *dolfile = (dolheader *)dolstart;
    u32 i;
    for (i = 0; i < 7; i++) {
        if (!dolfile->text_size[i] || dolfile->text_start[i] < 0x100) continue;
        ICInvalidateRange((void *)dolfile->text_start[i], dolfile->text_size[i]);
        memmove((void *)dolfile->text_start[i], dolstart+dolfile->text_pos[i], dolfile->text_size[i]);
    }
    for(i = 0; i < 11; i++) {
        if (!dolfile->data_size[i] || dolfile->data_start[i] < 0x100) continue;
        memmove((void*)dolfile->data_start[i], dolstart+dolfile->data_pos[i], dolfile->data_size[i]);
        DCFlushRangeNoSync((void *)dolfile->data_start[i], dolfile->data_size[i]);
    }
    memset((void *)dolfile->bss_start, 0, dolfile->bss_size);
    DCFlushRange((void *)dolfile->bss_start, dolfile->bss_size);
    return dolfile->entry_point;
}

void load_from_file(FILE *f) {
    struct stat st;
    int fd = fileno(f);
    if (fstat(fd, &st)) return;

    u8 *buf = malloc(st.st_size);
    if (!buf) return;
    void (*ep)();
    u32 level;
    
    if (!read_from_file(buf, f)) return;
    ep = (void(*)())load_dol_image(buf);

    __IOS_ShutdownSubsystems();
    _CPU_ISR_Disable(level);
    __exception_closeall();
    ep();
    _CPU_ISR_Restore(level);
    free(buf);
}
