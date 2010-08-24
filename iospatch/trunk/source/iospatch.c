// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <stdio.h>

#include "iospatch.h"

#define MEM_PROT 0xd8b420a

static bool have_ahbprot() {
    return read32(0xcd800064) == 0xffffffff;
}

static void disable_memory_protection() {
    write32(MEM_PROT, read32(MEM_PROT) & 0x0000FFFF);
}

static bool apply_patch(char *name, u32 start, const u32 *old, const u32 *new, u32 words) {
    u32 i;
    printf("Attempting to apply patch %s at 0x%08x ... ", name, start);
    for (i = 0; i < words; i++) {
        u32 val = read32(start + (i << 2));
        if (val != old[i]) {
            printf("not found.\n");
            return false;
        }
    }
    fflush(stdout);
    for (i = 0; i < words; i++) {
        write32(start + (i << 2), new[i]);
    }
    printf("done!\n");
    return true;
}

static const u32 di_readlimit_start = 0x139b66d8;
static const u32 di_readlimit_old[] = {
    0x00000000, 0x00000000, 0x00000000,
    0x00014000, 0x00000000, 0x460a0000, 
    0x00000000, 0x00000008, 0x00000000,
    0x7ed40000, 0x00000000, 0x00000008
};
static const u32 di_readlimit_new[] = {
    0x00000000, 0x00000000, 0x00000000,
    0x7ed44000, 0x00000000, 0x460a0000, 
    0x00000000, 0x00000008, 0x00000000,
    0x7ed40000, 0x00000000, 0x00000008
};

static const u32 isfs_permissions_start = 0x13a11304;
const u32 isfs_permissions_old[] = { 0x428bd001, 0x2566426d };
const u32 isfs_permissions_new[] = { 0x428be001, 0x2566426d };

u32 IOSPATCH_Apply() {
    u32 patches = 0;
    if (have_ahbprot()) {
        disable_memory_protection();
        patches += apply_patch("di_readlimit", di_readlimit_start, di_readlimit_old, di_readlimit_new, sizeof(di_readlimit_old) >> 2);
        patches += apply_patch("isfs_permissions", isfs_permissions_start, isfs_permissions_old, isfs_permissions_new, sizeof(isfs_permissions_old) >> 2);
    }
    return patches;
}
