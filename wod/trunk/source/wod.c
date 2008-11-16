/*

libwod -- a DVD image devoptab library for the Wii

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
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/iosupport.h>

#include "wod.h"

#define DISC_SIZE_GAMECUBE 1459978240LL
#define DISC_SIZE_WII_SINGLE 4699979776LL
#define DISC_SIZE_WII_DUAL 8511160320LL

#define REGION_CODE_USA 'E'
#define REGION_CODE_PAL 'P'
#define REGION_CODE_JAP 'J'

static const char *REGION_NAME_USA = "USA";
static const char *REGION_NAME_PAL = "PAL";
static const char *REGION_NAME_JAP = "JAP";
static const char *REGION_NAME_UNKNOWN = "Unknown region";

#define DISC_TYPE_GC          'G'
#define DISC_TYPE_GC_WIIKEY   'W'
#define DISC_TYPE_GC_UTIL     'U'
#define DISC_TYPE_GC_DEMO     'D'
#define DISC_TYPE_GC_PROMO    'P'
#define DISC_TYPE_WII         'R'
#define DISC_TYPE_WII_AUTO    '0'
#define DISC_TYPE_WII_UNKNOWN '1'
#define DISC_TYPE_WII_BACKUP  '4'
#define DISC_TYPE_WII_WIIFIT  '_'

static const char *DISC_NAME_GC          = "GameCube";
static const char *DISC_NAME_GC_WIIKEY   = "GameCube WiiKey config";
static const char *DISC_NAME_GC_UTIL     = "GameCube util";
static const char *DISC_NAME_GC_DEMO     = "GameCube demo";
static const char *DISC_NAME_GC_PROMO    = "GameCube promo";
static const char *DISC_NAME_WII         = "Wii";
static const char *DISC_NAME_WII_AUTO    = "Wii auto-boot";
static const char *DISC_NAME_WII_UNKNOWN = "Wii unknown";
static const char *DISC_NAME_WII_BACKUP  = "Wii backup";
static const char *DISC_NAME_WII_WIIFIT  = "WiiFit channel installer";
static const char *DISC_NAME_WII_DUAL    = "Wii dual-layer";
static const char *DISC_NAME_UNKNOWN     = "Unknown disc type";

static const u8 DISC_VERSION_SINGLE = 0;
static const u8 DISC_VERSION_DUAL = 1;

#define DIR_SEPARATOR '/'
#define SECTOR_SIZE 0x800
#define BUFFER_SIZE 0x8000

typedef struct {
    char name[WOD_MAXPATHLEN];
    u64 size;
    bool enabled;
} DIR_ENTRY;

typedef struct {
    DIR_ENTRY *entry;
    u64 offset;
    bool inUse;
} FILE_STRUCT;

typedef struct {
    DIR_ENTRY *entry;
    u32 index;
    bool inUse;
} DIR_STATE_STRUCT;

static DIR_ENTRY entries[] = {
    { "", 0, true },
    { "gc.img", DISC_SIZE_GAMECUBE, false },
    { "wii.img", DISC_SIZE_WII_SINGLE, false },
    { "wii_dual.img", DISC_SIZE_WII_DUAL, false },
    { "", 0, false }
};
static const u32 FILE_COUNT = sizeof(entries) / sizeof(DIR_ENTRY);

static unsigned char read_buffer[BUFFER_SIZE] __attribute__((aligned(32)));

static DIR_ENTRY *entry_from_path(const char *path) {
    if (strchr(path, ':') != NULL) path = strchr(path, ':') + 1;
    const char *pathPosition = path;
    const char *pathEnd = strchr(path, '\0');
    if (pathPosition[0] == DIR_SEPARATOR) {
        while (pathPosition[0] == DIR_SEPARATOR) pathPosition++;
        if (pathPosition >= pathEnd) return &entries[0];
    }
    if (!strcmp(".", pathPosition)) return &entries[0];
    u32 i;
    for (i = 1; i < FILE_COUNT; i++) {
        if (!strcasecmp(pathPosition, entries[i].name)) {
            if (!entries[i].enabled) return NULL;
            return &entries[i];
        }
    }
    return NULL;
}

static int _WOD_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode) {
    FILE_STRUCT *file = (FILE_STRUCT *)fileStruct;
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    } else if (entry == &entries[0]) {
        r->_errno = EISDIR;
        return -1;
    }
    
    file->entry = entry;
    file->offset = 0;
    file->inUse = true;

    return (int)file;
}

static int _WOD_close_r(struct _reent *r, int fd) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    file->inUse = false;
    return 0;
}

static int _WOD_read_r(struct _reent *r, int fd, char *ptr, int len) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    if (file->offset >= file->entry->size) {
        r->_errno = EOVERFLOW;
        return 0;
    }
    if (len + file->offset > file->entry->size) {
        r->_errno = EOVERFLOW;
        len = file->entry->size - file->offset;
    }
    if (len <= 0) {
        return 0;
    }

    u32 sector = file->offset / SECTOR_SIZE;
    u32 end_sector = (file->offset + len - 1) / SECTOR_SIZE;
    u32 sectors = MIN(BUFFER_SIZE / SECTOR_SIZE, end_sector - sector + 1);
    u32 sector_offset = file->offset % SECTOR_SIZE;
    len = MIN(BUFFER_SIZE - sector_offset, len);
    if (DI_ReadDVD(read_buffer, sectors, sector)) {
        r->_errno = EIO;
        return -1;
    }
    memcpy(ptr, read_buffer + sector_offset, len);
    file->offset += len;
    return len;
}

static int _WOD_seek_r(struct _reent *r, int fd, int pos, int dir) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }

    int position;

    switch (dir) {
        case SEEK_SET:
            position = pos;
            break;
        case SEEK_CUR:
            position = file->offset + pos;
            break;
        case SEEK_END:
            position = file->entry->size + pos;
            break;
        default:
            r->_errno = EINVAL;
            return -1;
    }
    
    if ((pos > 0) && (position < 0)) {
        r->_errno = EOVERFLOW;
        return -1;
    }

    if (position < 0 || position > file->entry->size) {
        r->_errno = EINVAL;
        return -1;
    }

    file->offset = position;

    return position;
}

static void stat_entry(DIR_ENTRY *entry, struct stat *st) {
    st->st_dev = WOD_DEVICE;
    st->st_ino = 0;
    st->st_mode = ((entry == &entries[0]) ? S_IFDIR : S_IFREG) | (S_IRUSR | S_IRGRP | S_IROTH);
    st->st_nlink = 1;
    st->st_uid = 1;
    st->st_gid = 2;
    st->st_rdev = st->st_dev;
    st->st_size = entry->size;
    st->st_atime = 0;
    st->st_spare1 = 0;
    st->st_mtime = 0;
    st->st_spare2 = 0;
    st->st_ctime = 0;
    st->st_spare3 = 0;
    st->st_blksize = SECTOR_SIZE;
    st->st_blocks = (entry->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    st->st_spare4[0] = 0;
    st->st_spare4[1] = 0;
}

static int _WOD_fstat_r(struct _reent *r, int fd, struct stat *st) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    stat_entry(file->entry, st);
    return 0;
}

static int _WOD_stat_r(struct _reent *r, const char *path, struct stat *st) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    }
    stat_entry(entry, st);
    return 0;
}

static int _WOD_chdir_r(struct _reent *r, const char *path) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    } else if (entry != &entries[0]) {
        r->_errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static DIR_ITER *_WOD_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    state->entry = entry_from_path(path);
    if (!state->entry) {
        r->_errno = ENOENT;
        return NULL;
    } else if (state->entry != &entries[0]) {
        r->_errno = ENOTDIR;
        return NULL;
    }
    state->index = 1;
    state->inUse = true;
    return dirState;
}

static int _WOD_dirreset_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->index = 1;
    return 0;
}

static int _WOD_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    DIR_ENTRY *entry = NULL;
    while (state->index < FILE_COUNT) {
        entry = &entries[state->index++];
        if (entry->enabled) break;
        entry = NULL;
    }
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    }
    strcpy(filename, entry->name);
    stat_entry(entry, st);
    return 0;
}

static int _WOD_dirclose_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->inUse = false;
    return 0;
}

static int _WOD_statvfs_r(struct _reent *r, const char *path, struct statvfs *buf) {
    r->_errno = ENOTSUP;
    return -1;
}

static int _WOD_write_r(struct _reent *r, int fd, const char *ptr, int len) {
    r->_errno = EBADF;
    return -1;
}

static int _WOD_link_r(struct _reent *r, const char *existing, const char *newLink) {
    r->_errno = EROFS;
    return -1;
}

static int _WOD_unlink_r(struct _reent *r, const char *path) {
    r->_errno = EROFS;
    return -1;
}

static int _WOD_rename_r(struct _reent *r, const char *oldName, const char *newName) {
    r->_errno = EROFS;
    return -1;
}

static int _WOD_mkdir_r(struct _reent *r, const char *path, int mode) {
    r->_errno = EROFS;
    return -1;
}

static const devoptab_t dotab_wod = {
    "wod",
    sizeof(FILE_STRUCT),
    _WOD_open_r,
    _WOD_close_r,
    _WOD_write_r,
    _WOD_read_r,
    _WOD_seek_r,
    _WOD_fstat_r,
    _WOD_stat_r,
    _WOD_link_r,
    _WOD_unlink_r,
    _WOD_chdir_r,
    _WOD_rename_r,
    _WOD_mkdir_r,
    sizeof(DIR_STATE_STRUCT),
    _WOD_diropen_r,
    _WOD_dirreset_r,
    _WOD_dirnext_r,
    _WOD_dirclose_r,
    _WOD_statvfs_r
};

struct dvd_header_struct {
    u8 disc_id;
    u8 game_code[2];
    u8 region_code;
    u8 maker_code[2];
    u8 disc_id2;
    u8 disc_version;
    u8 audio_streaming;
    u8 streaming_buffer_size;
    u8 unused[14];
    u8 magic[4];
    u8 unused2[4];
    u8 title[64];
    u8 disable_hashes;
    u8 disable_encryption;
} __attribute__((packed));

typedef struct {
    struct dvd_header_struct header;
    u64 size;
    const char *disc_type;
    const char *region;
} dvd_info;

static bool get_dvd_info(dvd_info *info) {
    if (DI_ReadDVD(read_buffer, 1, 0)) return false;
    memcpy(&info->header, read_buffer, sizeof(struct dvd_header_struct));
    info->size = DISC_SIZE_WII_SINGLE;
    info->disc_type = DISC_NAME_UNKNOWN;
    if (info->header.disc_version == DISC_VERSION_DUAL) {
        info->disc_type = DISC_NAME_WII_DUAL;
        info->size = DISC_SIZE_WII_DUAL;
    } else {
        switch (info->header.disc_id) {
            case DISC_TYPE_GC:          info->size = DISC_SIZE_GAMECUBE; info->disc_type = DISC_NAME_GC;        break;
            case DISC_TYPE_GC_WIIKEY:   info->size = DISC_SIZE_GAMECUBE; info->disc_type = DISC_NAME_GC_WIIKEY; break;
            case DISC_TYPE_GC_UTIL:     info->size = DISC_SIZE_GAMECUBE; info->disc_type = DISC_NAME_GC_UTIL;   break;
            case DISC_TYPE_GC_DEMO:     info->size = DISC_SIZE_GAMECUBE; info->disc_type = DISC_NAME_GC_DEMO;   break;
            case DISC_TYPE_GC_PROMO:    info->size = DISC_SIZE_GAMECUBE; info->disc_type = DISC_NAME_GC_PROMO;  break;
            case DISC_TYPE_WII:         info->disc_type = DISC_NAME_WII;         break;
            case DISC_TYPE_WII_AUTO:    info->disc_type = DISC_NAME_WII_AUTO;    break;
            case DISC_TYPE_WII_UNKNOWN: info->disc_type = DISC_NAME_WII_UNKNOWN; break;
            case DISC_TYPE_WII_BACKUP:  info->disc_type = DISC_NAME_WII_BACKUP;  break;
            case DISC_TYPE_WII_WIIFIT:  info->disc_type = DISC_NAME_WII_WIIFIT;  break;
        }
    }
    switch (info->header.region_code) {
        case REGION_CODE_USA: info->region = REGION_NAME_USA;     break;
        case REGION_CODE_PAL: info->region = REGION_NAME_PAL;     break;
        case REGION_CODE_JAP: info->region = REGION_NAME_JAP;     break;
        default:              info->region = REGION_NAME_UNKNOWN; break;
    }
    return true;
}

bool WOD_Mount() {
    WOD_Unmount();
    dvd_info info;
    if (!get_dvd_info(&info)) return false;
    if (info.disc_type == DISC_NAME_UNKNOWN) {
        u32 i;
        for (i = 1; i < (FILE_COUNT - 1); i++) {
            entries[i].enabled = true;
        }
    } else {
        entries[FILE_COUNT - 1].enabled = true;
        sprintf(entries[FILE_COUNT - 1].name, "%c%c%c%c%c%c %s (%s) [%s].img",
            info.header.disc_id, info.header.game_code[0], info.header.game_code[1], info.header.region_code, info.header.maker_code[0], info.header.maker_code[1],
            info.header.title, info.disc_type, info.region);
        entries[FILE_COUNT - 1].size = info.size;
    }
    return AddDevice(&dotab_wod) >= 0;
}

bool WOD_Unmount() {
    entries[FILE_COUNT - 1].name[0] = '\x00';
    entries[FILE_COUNT - 1].size = 0;
    u32 i;
    for (i = 1; i < FILE_COUNT; i++) {
        entries[i].enabled = false;
    }
    return !RemoveDevice("wod:/");
}
