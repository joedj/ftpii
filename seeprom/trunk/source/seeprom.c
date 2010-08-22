// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <errno.h>
#include <ogcsys.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>

#include "seeprom.h"
#include "mini_seeprom.h"

#define DEVICE_NAME "seeprom"

#define SEEPROM_SIZE 256
#define BLOCK_SIZE 2
#define DIR_SEPARATOR '/'

typedef struct {
    char name[SEEPROM_MAXPATHLEN];
    u32 size;
    u8 addr;
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
    { "", 0, 0 },
    { "seeprom.bin", 256, 0 },
    { "ms_id", 4, 0 },
    { "ca_id", 4, 4 },
    { "ng_id", 4, 8 },
    { "ng_sig", 60, 12 },
    { "counters1.1", 2, 72 },
    { "counters1.2", 2, 74 },
    { "counters1.3", 2, 76 },
    { "counters1.4", 2, 78 },
    { "counters1.5", 2, 80 },
    { "counters2.1", 2, 92 },
    { "counters2.2", 2, 94 },
    { "counters2.3", 2, 96 },
    { "korean_key", 16, 98 }
};
static const u32 FILE_COUNT = sizeof(entries) / sizeof(DIR_ENTRY);

static u8 seeprom[SEEPROM_SIZE];
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

static int _SEEPROM_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode) {
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

static int _SEEPROM_close_r(struct _reent *r, int fd) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    file->inUse = false;
    return 0;
}

static int _SEEPROM_read_r(struct _reent *r, int fd, char *ptr, size_t len) {
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

    memcpy(ptr, seeprom + file->entry->addr + file->offset, len);
    file->offset += len;
    return len;
}

static off_t _SEEPROM_seek_r(struct _reent *r, int fd, off_t pos, int dir) {
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
    st->st_dev = 0x4f54;
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
    st->st_blksize = BLOCK_SIZE;
    st->st_blocks = (entry->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    st->st_spare4[0] = 0;
    st->st_spare4[1] = 0;
}

static int _SEEPROM_fstat_r(struct _reent *r, int fd, struct stat *st) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    stat_entry(file->entry, st);
    return 0;
}

static int _SEEPROM_stat_r(struct _reent *r, const char *path, struct stat *st) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    }
    stat_entry(entry, st);
    return 0;
}

static int _SEEPROM_chdir_r(struct _reent *r, const char *path) {
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

static DIR_ITER *_SEEPROM_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path) {
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

static int _SEEPROM_dirreset_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->index = 1;
    return 0;
}

static int _SEEPROM_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st) {
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

static int _SEEPROM_dirclose_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->inUse = false;
    return 0;
}

static const devoptab_t dotab_seeprom = {
    DEVICE_NAME,
    sizeof(FILE_STRUCT),
    _SEEPROM_open_r,
    _SEEPROM_close_r,
    NULL,
    _SEEPROM_read_r,
    _SEEPROM_seek_r,
    _SEEPROM_fstat_r,
    _SEEPROM_stat_r,
    NULL,
    NULL,
    _SEEPROM_chdir_r,
    NULL,
    NULL,
    sizeof(DIR_STATE_STRUCT),
    _SEEPROM_diropen_r,
    _SEEPROM_dirreset_r,
    _SEEPROM_dirnext_r,
    _SEEPROM_dirclose_r,
    NULL
};

static bool read_seeprom() {
    return seeprom_read(seeprom, 0, SEEPROM_SIZE / 2) >= 0 && *(((u32 *)seeprom) + 2) != 0;
}

bool SEEPROM_Mount() {
    SEEPROM_Unmount();
    bool success = read_seeprom() && (dotab_device = AddDevice(&dotab_seeprom)) >= 0;
    if (!success) SEEPROM_Unmount();
    return success;
}

bool SEEPROM_Unmount() {
    memset(seeprom, 0, SEEPROM_SIZE);
    if (dotab_device >= 0) {
        dotab_device = -1;
        return !RemoveDevice(DEVICE_NAME ":");
    }
    return true;
}
