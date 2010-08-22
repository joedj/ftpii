// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <errno.h>
#include <ogcsys.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>

#include "otp.h"

#define DEVICE_NAME "otp"

#define HW_OTP_COMMAND (*(volatile u32 *)0xcd8001ec)
#define HW_OTP_DATA (*(volatile u32 *)0xcd8001f0)

#define OTP_SIZE 128
#define BLOCK_SIZE 32
#define DIR_SEPARATOR '/'

typedef struct {
    char name[OTP_MAXPATHLEN];
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
    { "otp.bin", 128, 0 },
    { "boot1_hash.bin", 20, 0 },
    { "common_key.bin", 16, 5 },
    { "ng_id.bin", 4, 9 },
    { "ng_private_key.bin", 32, 10 },
    { "nand_hmac.bin", 20, 17 },
    { "nand_aes.bin", 16, 22 },
    { "rng_key.bin", 16, 26 },
    { "unknown.bin", 8, 30 }
};
static const u32 FILE_COUNT = sizeof(entries) / sizeof(DIR_ENTRY);

static u8 otp[OTP_SIZE];
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

static int _OTP_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode) {
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

static int _OTP_close_r(struct _reent *r, int fd) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    file->inUse = false;
    return 0;
}

static int _OTP_read_r(struct _reent *r, int fd, char *ptr, size_t len) {
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

    memcpy(ptr, otp + (file->entry->addr * 4) + file->offset, len);
    file->offset += len;
    return len;
}

static off_t _OTP_seek_r(struct _reent *r, int fd, off_t pos, int dir) {
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

static int _OTP_fstat_r(struct _reent *r, int fd, struct stat *st) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    stat_entry(file->entry, st);
    return 0;
}

static int _OTP_stat_r(struct _reent *r, const char *path, struct stat *st) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    }
    stat_entry(entry, st);
    return 0;
}

static int _OTP_chdir_r(struct _reent *r, const char *path) {
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

static DIR_ITER *_OTP_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path) {
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

static int _OTP_dirreset_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->index = 1;
    return 0;
}

static int _OTP_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st) {
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

static int _OTP_dirclose_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->inUse = false;
    return 0;
}

static const devoptab_t dotab_otp = {
    DEVICE_NAME,
    sizeof(FILE_STRUCT),
    _OTP_open_r,
    _OTP_close_r,
    NULL,
    _OTP_read_r,
    _OTP_seek_r,
    _OTP_fstat_r,
    _OTP_stat_r,
    NULL,
    NULL,
    _OTP_chdir_r,
    NULL,
    NULL,
    sizeof(DIR_STATE_STRUCT),
    _OTP_diropen_r,
    _OTP_dirreset_r,
    _OTP_dirnext_r,
    _OTP_dirclose_r,
    NULL
};

static bool read_otp() {
    u8 addr;
    for (addr = 0; addr < 32; addr++) {
        HW_OTP_COMMAND = 0x80000000 | addr;
        *(((u32 *)otp) + addr) = HW_OTP_DATA;
    }
    return *otp;
}

bool OTP_Mount() {
    OTP_Unmount();
    bool success = read_otp() && (dotab_device = AddDevice(&dotab_otp)) >= 0;
    if (!success) OTP_Unmount();
    return success;
}

bool OTP_Unmount() {
    memset(otp, 0, OTP_SIZE);
    if (dotab_device >= 0) {
        dotab_device = -1;
        return !RemoveDevice(DEVICE_NAME ":");
    }
    return true;
}
