/*

libiso -- an ISO9660 DVD devoptab library for the Wii

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

#include "iso.h"

#define DIR_SEPARATOR '/'
#define SECTOR_SIZE 0x800
#define BUFFER_SIZE 0x8000

typedef struct DIR_ENTRY_STRUCT {
    char name[ISO_MAXPATHLEN];
    u8 flags;
    u32 sector;
    u32 size;
    u32 fileCount;
    struct DIR_ENTRY_STRUCT *children;
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

static u8 read_buffer[BUFFER_SIZE] __attribute__((aligned(32)));

static struct {
    DIR_ENTRY *root;
    DIR_ENTRY *current;
    bool unicode;
} mountState;

struct pvd_s {
    char id[8];
    char system_id[32];
    char volume_id[32];
    char zero[8];
    unsigned long total_sector_le, total_sect_be;
    char zero2[32];
    unsigned long volume_set_size, volume_seq_nr;
    unsigned short sector_size_le, sector_size_be;
    unsigned long path_table_len_le, path_table_len_be;
    unsigned long path_table_le, path_table_2nd_le;
    unsigned long path_table_be, path_table_2nd_be;
    u8 root[34];
    char volume_set_id[128], publisher_id[128], data_preparer_id[128], application_id[128];
    char copyright_file_id[37], abstract_file_id[37], bibliographical_file_id[37];
}  __attribute__((packed));

static bool is_dir(DIR_ENTRY *entry) {
    return entry->flags & 2;
}

static DIR_ENTRY *entry_from_path(const char *path) {
    if (strchr(path, ':') != NULL) {
        path = strchr(path, ':') + 1;
    }
    DIR_ENTRY *entry;
    bool found = false;
    bool notFound = false;
    const char *pathPosition = path;
    const char *pathEnd = strchr(path, '\0');
    if (pathPosition[0] == DIR_SEPARATOR) {
        entry = mountState.root;
        while (pathPosition[0] == DIR_SEPARATOR) {
            pathPosition++;
        }
        if (pathPosition >= pathEnd) {
            found = true;
        }
    } else {
        entry = mountState.current;
    }
    if (entry == mountState.root && !strcmp(".", pathPosition)) {
        found = true;
    }
    DIR_ENTRY *dir = entry;
    while (!found && !notFound) {
        const char *nextPathPosition = strchr(pathPosition, DIR_SEPARATOR);
        size_t dirnameLength;
        if (nextPathPosition != NULL) {
            dirnameLength = nextPathPosition - pathPosition;
        } else {
            dirnameLength = strlen(pathPosition);
        }

        if (dirnameLength >= ISO_MAXPATHLEN) return NULL;

        u32 fileIndex = 0;
        while (fileIndex < dir->fileCount && !found && !notFound) {
            entry = &dir->children[fileIndex];
            if (dirnameLength == strnlen(entry->name, ISO_MAXPATHLEN - 1) && !strncasecmp(pathPosition, entry->name, dirnameLength)) found = true;
            if (found && !is_dir(entry) && nextPathPosition) found = false;
            if (!found) fileIndex++;
        }

        if (fileIndex >= dir->fileCount) {
            notFound = true;
            found = false;
        } else if (!nextPathPosition || nextPathPosition >= pathEnd) {
            found = true;
        } else if (is_dir(entry)) {
            dir = entry;
            pathPosition = nextPathPosition;
            while (pathPosition[0] == DIR_SEPARATOR) pathPosition++;
            if (pathPosition >= pathEnd) found = true;
            else found = false;
        }
    }

    if (found && !notFound) return entry;
    return NULL;
}

static int _ISO9660_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode) {
    FILE_STRUCT *file = (FILE_STRUCT *)fileStruct;
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    } else if (is_dir(entry)) {
        r->_errno = EISDIR;
        return -1;
    }
    
    file->entry = entry;
    file->offset = 0;
    file->inUse = true;

    return (int)file;
}

static int _ISO9660_close_r(struct _reent *r, int fd) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    file->inUse = false;
    return 0;
}

static int _ISO9660_read_r(struct _reent *r, int fd, char *ptr, int len) {
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

    u32 sector = file->entry->sector + file->offset / SECTOR_SIZE;
    u32 end_sector = (file->entry->sector * SECTOR_SIZE + file->offset + len - 1) / SECTOR_SIZE;
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

static int _ISO9660_seek_r(struct _reent *r, int fd, int pos, int dir) {
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
    st->st_dev = 69;
    st->st_ino = (ino_t)entry->sector;
    st->st_mode = (is_dir(entry) ? S_IFDIR : S_IFREG) | (S_IRUSR | S_IRGRP | S_IROTH);
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

static int _ISO9660_fstat_r(struct _reent *r, int fd, struct stat *st) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    stat_entry(file->entry, st);
    return 0;
}

static int _ISO9660_stat_r(struct _reent *r, const char *path, struct stat *st) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    }
    stat_entry(entry, st);
    return 0;
}

static int _ISO9660_chdir_r(struct _reent *r, const char *path) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    } else if (!is_dir(entry)) {
        r->_errno = ENOTDIR;
        return -1;
    }
    mountState.current = entry;
    return 0;
}

static DIR_ITER *_ISO9660_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    state->entry = entry_from_path(path);
    if (!state->entry) {
        r->_errno = ENOENT;
        return NULL;
    } else if (!is_dir(state->entry)) {
        r->_errno = ENOTDIR;
        return NULL;
    }
    state->index = 0;
    state->inUse = true;
    return dirState;
}

static int _ISO9660_dirreset_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->index = 0;
    return 0;
}

static int _ISO9660_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    if (state->index >= state->entry->fileCount) {
        r->_errno = ENOENT;
        return -1;
    }
    DIR_ENTRY *entry = &state->entry->children[state->index++];
    strncpy(filename, entry->name, ISO_MAXPATHLEN - 1);
    stat_entry(entry, st);
    return 0;
}

static int _ISO9660_dirclose_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->inUse = false;
    return 0;
}

static int _ISO9660_statvfs_r(struct _reent *r, const char *path, struct statvfs *buf) {
    r->_errno = ENOTSUP;
    return -1;
}

static int _ISO9660_write_r(struct _reent *r, int fd, const char *ptr, int len) {
    r->_errno = EBADF;
    return -1;
}

static int _ISO9660_link_r(struct _reent *r, const char *existing, const char *newLink) {
    r->_errno = EROFS;
    return -1;
}

static int _ISO9660_unlink_r(struct _reent *r, const char *path) {
    r->_errno = EROFS;
    return -1;
}

static int _ISO9660_rename_r(struct _reent *r, const char *oldName, const char *newName) {
    r->_errno = EROFS;
    return -1;
}

static int _ISO9660_mkdir_r(struct _reent *r, const char *path, int mode) {
    r->_errno = EROFS;
    return -1;
}

static const devoptab_t dotab_iso9660 = {
    "dvd",
    sizeof(FILE_STRUCT),
    _ISO9660_open_r,
    _ISO9660_close_r,
    _ISO9660_write_r,
    _ISO9660_read_r,
    _ISO9660_seek_r,
    _ISO9660_fstat_r,
    _ISO9660_stat_r,
    _ISO9660_link_r,
    _ISO9660_unlink_r,
    _ISO9660_chdir_r,
    _ISO9660_rename_r,
    _ISO9660_mkdir_r,
    sizeof(DIR_STATE_STRUCT),
    _ISO9660_diropen_r,
    _ISO9660_dirreset_r,
    _ISO9660_dirnext_r,
    _ISO9660_dirclose_r,
    _ISO9660_statvfs_r
};

#define OFFSET_SECTOR 6
#define OFFSET_SIZE 14
#define OFFSET_FLAGS 25
#define OFFSET_NAMELEN 32
#define OFFSET_NAME 33

static int read_entry(DIR_ENTRY *entry, u8 *buf) {
    u32 sector = buf[OFFSET_SECTOR] << 24 | buf[OFFSET_SECTOR + 1] << 16 | buf[OFFSET_SECTOR + 2] << 8 | buf[OFFSET_SECTOR + 3];
    u32 size = buf[OFFSET_SIZE] << 24 | buf[OFFSET_SIZE + 1] << 16 | buf[OFFSET_SIZE + 2] << 8 | buf[OFFSET_SIZE + 3];
    u8 flags = buf[OFFSET_FLAGS];
    u8 namelen = buf[OFFSET_NAMELEN];

    if (namelen == 1 && buf[OFFSET_NAME] == 1) {
        // ..
    } else if (namelen == 1 && !buf[OFFSET_NAME]) {
        entry->flags = flags;
        entry->sector = sector;
        entry->size = size;
    } else {
        DIR_ENTRY *newChildren = realloc(entry->children, sizeof(DIR_ENTRY) * (entry->fileCount + 1));
        if (!newChildren) return -1;
        bzero(newChildren + entry->fileCount, sizeof(DIR_ENTRY));
        entry->children = newChildren;
        DIR_ENTRY *child = &entry->children[entry->fileCount++];
        child->sector = sector;
        child->size = size;
        child->flags = flags;
        char *name = child->name;
        if (mountState.unicode) {
            u32 i;
            for (i = 0; i < (namelen / 2); ++i) name[i] = buf[OFFSET_NAME + i * 2 + 1];
            name[i] = '\x00';
            namelen = i;
        } else {
            memcpy(name, buf + OFFSET_NAME, namelen);
            name[namelen] = '\x00';
        }
        if (flags & 2) {
            name[namelen] = '\x00';
        } else {
            if (namelen >= 2 && name[namelen - 2] == ';') name[namelen - 2] = '\x00';
        }
    }

    return *buf;
}

static bool read_directory(DIR_ENTRY *entry) {
    u32 sector = entry->sector;
    u32 remaining = entry->size;
    entry->fileCount = 0;
    if (DI_ReadDVD(read_buffer, 1, sector)) return false;
    u32 sector_offset = 0;
    while (remaining > 0) {
        int offset = read_entry(entry, read_buffer + sector_offset);
        if (offset == -1) return false;
        sector_offset += offset;
        if (sector_offset >= SECTOR_SIZE || !read_buffer[sector_offset]) {
             remaining -= SECTOR_SIZE;
             if (DI_ReadDVD(read_buffer, 1, ++sector)) return false;
             sector_offset = 0;
        }
    }
    return true;
}

static bool read_recursive(DIR_ENTRY *entry) {
    if (!read_directory(entry)) return false;
    u32 i;
    for (i = 0; i < entry->fileCount; i++) {
        if (is_dir(&entry->children[i]) && !read_recursive(&entry->children[i])) return false;
    }
    return true;
}

static bool read_directories() {
    struct pvd_s *pvd = NULL;
    struct pvd_s *svd = NULL;
    u32 sector;
    for (sector = 16; sector < 32; sector++) {
        if (DI_ReadDVD(read_buffer, 1, sector)) return false;
        if (!memcmp(((struct pvd_s *)read_buffer)->id, "\2CD001\1", 8)) {
            svd = (struct pvd_s *)read_buffer;
            break;
        }
    }
    if (!svd) {
        for (sector = 16; sector < 32; sector++) {
            if (DI_ReadDVD(read_buffer, 1, sector)) return false;
            if (!memcmp(((struct pvd_s *)read_buffer)->id, "\1CD001\1", 8)) {
                pvd = (struct pvd_s *)read_buffer;
                break;
            }
        }
    }
    if (!pvd && !svd) return false;

    if (!(mountState.root = malloc(sizeof(DIR_ENTRY)))) return false;
    bzero(mountState.root, sizeof(DIR_ENTRY));
    mountState.current = mountState.root;
    if (svd) {
        mountState.unicode = true;
        if (read_entry(mountState.root, svd->root) == -1) return false;
    } else {
        mountState.unicode = false;
        if (read_entry(mountState.root, pvd->root) == -1) return false;
    }

    return read_recursive(mountState.root);
}

static void cleanup_recursive(DIR_ENTRY *entry) {
    u32 i;
    for (i = 0; i < entry->fileCount; i++) {
        if (is_dir(&entry->children[i])) cleanup_recursive(&entry->children[i]);
    }
    if (entry->children) free(entry->children);
}

bool ISO9660_Mount() {
    ISO9660_Unmount();
    return read_directories() && AddDevice(&dotab_iso9660) >= 0;
}

bool ISO9660_Unmount() {
    if (mountState.root) {
        cleanup_recursive(mountState.root);
        free(mountState.root);
        mountState.root = NULL;
    }
    mountState.current = mountState.root;
    mountState.unicode = false;
    return !RemoveDevice("dvd:/");
}
