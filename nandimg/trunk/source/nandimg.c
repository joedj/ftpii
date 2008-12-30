/*

libnandimg -- a NAND image devoptab library for the Wii

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
// #include <ogc/lwp_watchdog.h>
#include <ogcsys.h>
#include <stdio.h>
// #include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/iosupport.h>

#include "nandimg.h"

#define DEVICE_NAME "nand"

#define NAND_SIZE_WITH_ECC 69
#define NAND_SIZE_WITHOUT_ECC 64

#define DIR_SEPARATOR '/'
#define SECTOR_SIZE 0x800
#define BUFFER_SIZE 0x8000

typedef struct {
    char name[NANDIMG_MAXPATHLEN];
    u32 size;
} DIR_ENTRY;

typedef struct {
    DIR_ENTRY *entry;
    u32 offset;
    bool inUse;
} FILE_STRUCT;

typedef struct {
    DIR_ENTRY *entry;
    u32 index;
    bool inUse;
} DIR_STATE_STRUCT;

static DIR_ENTRY entries[] = {
    { "", 0 },
    { "nand_with_ecc.img", NAND_SIZE_WITH_ECC },
    { "nand_without_ecc.img", NAND_SIZE_WITHOUT_ECC }
};
static const u32 FILE_COUNT = sizeof(entries) / sizeof(DIR_ENTRY);

static u8 read_buffer[BUFFER_SIZE] __attribute__((aligned(32)));
static u32 cache_start = 0;
static u32 cache_sectors = 0;
static s32 dotab_device = -1;

static bool invalid_drive_specifier(const char *path) {
    if (strchr(path, ':') == NULL) return false;
	int namelen = strlen(DEVICE_NAME);
    if (!strncmp(DEVICE_NAME, path, namelen) && path[namelen] == ':') return false;
    return true;
}

static DIR_ENTRY *entry_from_path(const char *path) {
    if (invalid_drive_specifier(path)) return NULL;
    if (strchr(path, ':') != NULL) path = strchr(path, ':') + 1;
    const char *pathPosition = path;
    const char *pathEnd = strchr(path, '\0');
    if (pathPosition[0] == DIR_SEPARATOR) {
        while (pathPosition[0] == DIR_SEPARATOR) pathPosition++;
        if (pathPosition >= pathEnd) return &entries[0];
    }
    if (!strcmp(".", pathPosition)) return &entries[0];
    u32 i;
    for (i = 1; i < FILE_COUNT; i++)
        if (!strcasecmp(pathPosition, entries[i].name))
            return &entries[i];
    return NULL;
}

static int _NANDIMG_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode) {
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

static int _NANDIMG_close_r(struct _reent *r, int fd) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    file->inUse = false;
    return 0;
}

static bool read_nand(u8 *buf, u32 sectors, u32 sector) {
    return false; // err0r
}

static int _NANDIMG_read_r(struct _reent *r, int fd, char *ptr, size_t len) {
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
    u32 sector_offset = file->offset % SECTOR_SIZE;
    u32 sectors = MIN(BUFFER_SIZE / SECTOR_SIZE, end_sector - sector + 1);
    len = MIN(BUFFER_SIZE - sector_offset, len);

    if (cache_sectors && sector >= cache_start && (sector + sectors) <= (cache_start + cache_sectors)) {
        memcpy(ptr, read_buffer + (sector - cache_start) * SECTOR_SIZE + sector_offset, len);
        file->offset += len;
        return len;
    }

    u32 remaining_sectors = MIN(BUFFER_SIZE / SECTOR_SIZE, (file->entry->size - 1) / SECTOR_SIZE - sector + 1);
    if (!read_nand(read_buffer, remaining_sectors, sector)) {
        cache_sectors = 0;
        r->_errno = EIO;
        return -1;
    }
    cache_start = sector;
    cache_sectors = remaining_sectors;
    memcpy(ptr, read_buffer + sector_offset, len);
    file->offset += len;
    return len;
}

static off_t _NANDIMG_seek_r(struct _reent *r, int fd, off_t pos, int dir) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }

    s64 position;

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
    
    if (pos > 0 && position < 0) {
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
    st->st_dev = 0x434f;
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

static int _NANDIMG_fstat_r(struct _reent *r, int fd, struct stat *st) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    stat_entry(file->entry, st);
    return 0;
}

static int _NANDIMG_stat_r(struct _reent *r, const char *path, struct stat *st) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    }
    stat_entry(entry, st);
    return 0;
}

static int _NANDIMG_chdir_r(struct _reent *r, const char *path) {
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

static DIR_ITER *_NANDIMG_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path) {
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

static int _NANDIMG_dirreset_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->index = 1;
    return 0;
}

static int _NANDIMG_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    DIR_ENTRY *entry = NULL;
    if (state->index >= FILE_COUNT) {
        r->_errno = ENOENT;
        return -1;
    }
    entry = &entries[state->index++];
    strcpy(filename, entry->name);
    stat_entry(entry, st);
    return 0;
}

static int _NANDIMG_dirclose_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->inUse = false;
    return 0;
}

static const devoptab_t dotab_nandimg = {
    DEVICE_NAME,
    sizeof(FILE_STRUCT),
    _NANDIMG_open_r,
    _NANDIMG_close_r,
    NULL,
    _NANDIMG_read_r,
    _NANDIMG_seek_r,
    _NANDIMG_fstat_r,
    _NANDIMG_stat_r,
    NULL,
    NULL,
    _NANDIMG_chdir_r,
    NULL,
    NULL,
    sizeof(DIR_STATE_STRUCT),
    _NANDIMG_diropen_r,
    _NANDIMG_dirreset_r,
    _NANDIMG_dirnext_r,
    _NANDIMG_dirclose_r,
    NULL
};

bool NANDIMG_Mount() {
    NANDIMG_Unmount();
    bool success = (dotab_device = AddDevice(&dotab_nandimg)) >= 0;
    if (!success) NANDIMG_Unmount();
    return success;
}

bool NANDIMG_Unmount() {
    cache_sectors = 0;
    if (dotab_device >= 0) {
        dotab_device = -1;
        return !RemoveDevice(DEVICE_NAME ":/");
    }
    return true;
}
