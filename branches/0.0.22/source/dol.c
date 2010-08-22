// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
/*
This code is based heavily off the DOL-loading code in svpe's sdelfloader which contains the
following comment:
    this code was contributed by shagkur of the devkitpro team, thx!
*/
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <string.h>

#include "dol.h"

extern void __exception_closeall();

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

static u32 load_dol_image(const void *dolstart, struct __argv *argv) {
    dolheader *dolfile = (dolheader *)dolstart;
    u32 i;
    for (i = 0; i < 7; i++) {
        if (!dolfile->text_size[i] || dolfile->text_start[i] < 0x100) continue;
        ICInvalidateRange((void *)dolfile->text_start[i], dolfile->text_size[i]);
        memmove((void *)dolfile->text_start[i], dolstart+dolfile->text_pos[i], dolfile->text_size[i]);
    }
    for (i = 0; i < 11; i++) {
        if (!dolfile->data_size[i] || dolfile->data_start[i] < 0x100) continue;
        memmove((void *)dolfile->data_start[i], dolstart+dolfile->data_pos[i], dolfile->data_size[i]);
        DCFlushRangeNoSync((void *)dolfile->data_start[i], dolfile->data_size[i]);
    }

    if (argv && argv->argvMagic == ARGV_MAGIC) {
        void *new_argv = (void *)(dolfile->entry_point + 8);
        memmove(new_argv, argv, sizeof(*argv));
        DCFlushRange(new_argv, sizeof(*argv));
    }

    return dolfile->entry_point;
}

void run_dol(const void *dol, struct __argv *argv) {
    u32 level;
    void (*ep)() = (void(*)())load_dol_image(dol, argv);
    __IOS_ShutdownSubsystems();
    _CPU_ISR_Disable(level);
    __exception_closeall();
    ep();
    _CPU_ISR_Restore(level);
}
