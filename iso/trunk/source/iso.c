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
#include <ogc/lwp_watchdog.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/iosupport.h>

#include "iso.h"

#define DEVICE_NAME "dvd"

#define FLAG_DIR 2
#define DIR_SEPARATOR '/'
#define SECTOR_SIZE 0x800
#define BUFFER_SIZE 0x8000

typedef struct {
    u8 name_length;
    u8 extended_sectors;
    u32 sector;
    u16 parent;
    char name[ISO_MAXPATHLEN];
} __attribute__((packed)) PATHTABLE_ENTRY;

typedef struct PATH_ENTRY_STRUCT {
    PATHTABLE_ENTRY table_entry;
    u16 index;
    u32 childCount;
    struct PATH_ENTRY_STRUCT *children;
} PATH_ENTRY;

typedef struct DIR_ENTRY_STRUCT {
    char name[ISO_MAXPATHLEN];
    u32 sector;
    u32 size;
    u8 flags;
    u32 fileCount;
    PATH_ENTRY *path_entry;
    struct DIR_ENTRY_STRUCT *children;
} DIR_ENTRY;

typedef struct {
    DIR_ENTRY entry;
    u32 offset;
    bool inUse;
} FILE_STRUCT;

typedef struct {
    DIR_ENTRY entry;
    u32 index;
    bool inUse;
} DIR_STATE_STRUCT;

static u8 read_buffer[BUFFER_SIZE] __attribute__((aligned(32)));
static u8 cluster_buffer[BUFFER_SIZE] __attribute__((aligned(32)));
static u32 cache_start = 0;
static u32 cache_sectors = 0;

static PATH_ENTRY *root = NULL;
static PATH_ENTRY *current = NULL;
static bool unicode = false;
static u64 last_access = 0;
static s32 dotab_device = -1;

static bool is_dir(DIR_ENTRY *entry) {
    return entry->flags & FLAG_DIR;
}

#define OFFSET_EXTENDED 1
#define OFFSET_SECTOR 6
#define OFFSET_SIZE 14
#define OFFSET_FLAGS 25
#define OFFSET_NAMELEN 32
#define OFFSET_NAME 33

static int _read(void *ptr, u64 offset, size_t len) {
    u32 sector = offset / SECTOR_SIZE;
    u32 end_sector = (offset + len - 1) / SECTOR_SIZE;
    u32 sectors = MIN(BUFFER_SIZE / SECTOR_SIZE, end_sector - sector + 1);
    u32 sector_offset = offset % SECTOR_SIZE;
    len = MIN(BUFFER_SIZE - sector_offset, len);
    if (cache_sectors && sector >= cache_start && (sector + sectors) <= (cache_start + cache_sectors)) {
        memcpy(ptr, read_buffer + (sector - cache_start) * SECTOR_SIZE + sector_offset, len);
        return len;
    }
    if (DI_ReadDVD(read_buffer, BUFFER_SIZE / SECTOR_SIZE, sector)) {
        last_access = gettime();
        cache_sectors = 0;
        return -1;
    }
    last_access = gettime();
    cache_start = sector;
    cache_sectors = BUFFER_SIZE / SECTOR_SIZE;
    memcpy(ptr, read_buffer + sector_offset, len);
    return len;
}

static int read_entry(DIR_ENTRY *entry, u8 *buf) {
    u8 extended_sectors = buf[OFFSET_EXTENDED];
    u32 sector = *(u32 *)(buf + OFFSET_SECTOR) + extended_sectors;
    u32 size = *(u32 *)(buf + OFFSET_SIZE);
    u8 flags = buf[OFFSET_FLAGS];
    u8 namelen = buf[OFFSET_NAMELEN];

    if (namelen == 1 && buf[OFFSET_NAME] == 1) {
        // ..
    } else if (namelen == 1 && !buf[OFFSET_NAME]) {
        entry->sector = sector;
        entry->size = size;
        entry->flags = flags;
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
        if (unicode) {
            u32 i;
            for (i = 0; i < (namelen / 2); i++) name[i] = buf[OFFSET_NAME + i * 2 + 1];
            name[i] = '\x00';
            namelen = i;
        } else {
            memcpy(name, buf + OFFSET_NAME, namelen);
            name[namelen] = '\x00';
        }
        if (!(flags & FLAG_DIR) && namelen >= 2 && name[namelen - 2] == ';') name[namelen - 2] = '\x00';
    }

    return *buf;
}

static bool read_directory(DIR_ENTRY *dir_entry, PATH_ENTRY *path_entry) {
    u32 sector = path_entry->table_entry.sector;
    u32 remaining = 0;
    u32 sector_offset = 0;
    
    do {
        if (_read(cluster_buffer, (u64)sector * SECTOR_SIZE + sector_offset, (SECTOR_SIZE - sector_offset)) != (SECTOR_SIZE - sector_offset)) return false;
        int offset = read_entry(dir_entry, cluster_buffer);
        if (offset == -1) return false;
        if (!remaining) {
            remaining = dir_entry->size;
            dir_entry->path_entry = path_entry;
        }
        sector_offset += offset;
        if (sector_offset >= SECTOR_SIZE || !cluster_buffer[offset]) {
            remaining -= SECTOR_SIZE;
            sector_offset = 0;
            sector++;
        }
    } while (remaining > 0);
    
    return true;
}

static bool path_entry_from_path(PATH_ENTRY *path_entry, const char *path) {
    bool found = false;
    bool notFound = false;
    const char *pathPosition = path;
    const char *pathEnd = strchr(path, '\0');
    PATH_ENTRY *entry = root;
    while (pathPosition[0] == DIR_SEPARATOR) pathPosition++;
    if (pathPosition >= pathEnd) found = true;
    PATH_ENTRY *dir = entry;
    while (!found && !notFound) {
        const char *nextPathPosition = strchr(pathPosition, DIR_SEPARATOR);
        size_t dirnameLength;
        if (nextPathPosition != NULL) dirnameLength = nextPathPosition - pathPosition;
        else dirnameLength = strlen(pathPosition);
        if (dirnameLength >= ISO_MAXPATHLEN) return false;
    
        u32 childIndex = 0;
        while (childIndex < dir->childCount && !found && !notFound) {
            entry = &dir->children[childIndex];
            if (dirnameLength == strnlen(entry->table_entry.name, ISO_MAXPATHLEN - 1) && !strncasecmp(pathPosition, entry->table_entry.name, dirnameLength)) found = true;
            if (!found) childIndex++;
        }
    
        if (childIndex >= dir->childCount) {
            notFound = true;
            found = false;
        } else if (!nextPathPosition || nextPathPosition >= pathEnd) {
            found = true;
        } else {
            dir = entry;
            pathPosition = nextPathPosition;
            while (pathPosition[0] == DIR_SEPARATOR) pathPosition++;
            if (pathPosition >= pathEnd) found = true;
            else found = false;
        }
    }

    if (found) memcpy(path_entry, entry, sizeof(PATH_ENTRY));
    return found;
}

static bool find_in_directory(DIR_ENTRY *dir_entry, PATH_ENTRY *parent, const char *base) {
    u32 nl = strlen(base);

    // there will be no basename if we are looking for root
    if (!nl) {
        return read_directory(dir_entry, parent);
    }

    // check directories i know about first
    u32 childIndex;
    for (childIndex = 0; childIndex < parent->childCount; childIndex++) {
        PATH_ENTRY *child = parent->children + childIndex;
        if (nl == strnlen(child->table_entry.name, ISO_MAXPATHLEN - 1) && !strncasecmp(base, child->table_entry.name, nl)) {
            // found the thing we're after and it is a directory
            // read it into dir_entry, and return true
            return read_directory(dir_entry, child);
        }
    }
    
    // read ourselves into a DIR_ENTRY, look into children for matching file
    if (!read_directory(dir_entry, parent)) return false;
    for (childIndex = 0; childIndex < dir_entry->fileCount; childIndex++) {
        DIR_ENTRY *child = dir_entry->children + childIndex;
        if (nl == strnlen(child->name, ISO_MAXPATHLEN - 1) && !strncasecmp(base, child->name, nl)) {
            // found the thing we're after and it is a file
            // stick it in dir_entry, and return true
            memcpy(dir_entry, child, sizeof(DIR_ENTRY));
            return true;
        }
    }
    
    return false;
}

static char *dirname(char *path) {
    static char result[1024]; // TODO: find MAXPATHLEN
    strncpy(result, path, 1024 - 1);
    result[1024 - 1] = '\0';
    s32 i;
    for (i = strlen(result) - 1; i >= 0; i--) {
        if (result[i] == '/') {
            result[i] = '\0';
            return result;
        }
    }
    return "";
}

static char *basename(char *path) {
    s32 i;
    for (i = strlen(path) - 1; i >= 0; i--) {
        if (path[i] == '/') {
            return path + i + 1;
        }
    }
    return path;
}

static bool invalid_drive_specifier(const char *path) {
    if (strchr(path, ':') == NULL) return false;
	int namelen = strlen(DEVICE_NAME);
    if (!strncmp(DEVICE_NAME, path, namelen) && path[namelen] == ':') return false;
    return true;
}

static bool entry_from_path(DIR_ENTRY *dir_entry, const char *const_path) {
    bzero(dir_entry, sizeof(DIR_ENTRY));

    if (invalid_drive_specifier(const_path)) return false;

    // get rid of drive specifier
    if (strchr(const_path, ':') != NULL) const_path = strchr(const_path, ':') + 1;

    char path[strlen(const_path) + 1];
    strcpy(path, const_path);

    // strip trailing slashes except for root
    u32 len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\x00';
        
    }

    char *dir = dirname(path);
    char *base = basename(path);
    
    PATH_ENTRY parent_entry;
    if (!path_entry_from_path(&parent_entry, dir)) return false;
    bool found = find_in_directory(dir_entry, &parent_entry, base);
    if (!found && dir_entry->children) free(dir_entry->children);
    return found;
}

static int _ISO9660_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode) {
    FILE_STRUCT *file = (FILE_STRUCT *)fileStruct;
    DIR_ENTRY entry;
    if (!entry_from_path(&entry, path)) {
        r->_errno = ENOENT;
        return -1;
    } else if (is_dir(&entry)) {
        if (entry.children) free(entry.children);
        r->_errno = EISDIR;
        return -1;
    }
    
    memcpy(&file->entry, &entry, sizeof(DIR_ENTRY));
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

static int _ISO9660_read_r(struct _reent *r, int fd, char *ptr, size_t len) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    if (file->offset >= file->entry.size) {
        r->_errno = EOVERFLOW;
        return 0;
    }
    if (len + file->offset > file->entry.size) {
        r->_errno = EOVERFLOW;
        len = file->entry.size - file->offset;
    }
    if (len <= 0) {
        return 0;
    }

    u64 offset = file->entry.sector * SECTOR_SIZE + file->offset;
    if ((len = _read(ptr, offset, len)) < 0) {
        r->_errno = EIO;
        return -1;
    }
    
    file->offset += len;
    return len;
}

static off_t _ISO9660_seek_r(struct _reent *r, int fd, off_t pos, int dir) {
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
            position = file->entry.size + pos;
            break;
        default:
            r->_errno = EINVAL;
            return -1;
    }
    
    if (pos > 0 && position < 0) {
        r->_errno = EOVERFLOW;
        return -1;
    }

    if (position < 0 || position > file->entry.size) {
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
    stat_entry(&file->entry, st);
    return 0;
}

static int _ISO9660_stat_r(struct _reent *r, const char *path, struct stat *st) {
    DIR_ENTRY entry;
    if (!entry_from_path(&entry, path)) {
        r->_errno = ENOENT;
        return -1;
    }
    stat_entry(&entry, st);
    if (entry.children) free(entry.children);
    return 0;
}

static int _ISO9660_chdir_r(struct _reent *r, const char *path) {
    DIR_ENTRY entry;
    if (!entry_from_path(&entry, path)) {
        r->_errno = ENOENT;
        return -1;
    } else if (!is_dir(&entry)) {
        r->_errno = ENOTDIR;
        return -1;
    }
    current = entry.path_entry;
    if (entry.children) free(entry.children);
    return 0;
}

static DIR_ITER *_ISO9660_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!entry_from_path(&state->entry, path)) {
        r->_errno = ENOENT;
        return NULL;
    } else if (!is_dir(&state->entry)) {
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
    if (state->index >= state->entry.fileCount) {
        r->_errno = ENOENT;
        return -1;
    }
    DIR_ENTRY *entry = &state->entry.children[state->index++];
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
    if (state->entry.children) free(state->entry.children);
    return 0;
}

static const devoptab_t dotab_iso9660 = {
    DEVICE_NAME,
    sizeof(FILE_STRUCT),
    _ISO9660_open_r,
    _ISO9660_close_r,
    NULL,
    _ISO9660_read_r,
    _ISO9660_seek_r,
    _ISO9660_fstat_r,
    _ISO9660_stat_r,
    NULL,
    NULL,
    _ISO9660_chdir_r,
    NULL,
    NULL,
    sizeof(DIR_STATE_STRUCT),
    _ISO9660_diropen_r,
    _ISO9660_dirreset_r,
    _ISO9660_dirnext_r,
    _ISO9660_dirclose_r,
    NULL
};

typedef struct {
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
} __attribute__((packed)) VOLUME_DESCRIPTOR;

static VOLUME_DESCRIPTOR *read_volume_descriptor(u8 descriptor) {
    u8 sector;
    for (sector = 16; sector < 32; sector++) {
        if (DI_ReadDVD(read_buffer, 1, sector)) return NULL;
        if (!memcmp(read_buffer + 1, "CD001\1", 6)) {
            if (*read_buffer == descriptor) return (VOLUME_DESCRIPTOR *)read_buffer;
            else if (*read_buffer == 0xff) return NULL;
        }

    }
    return NULL;
}

static PATH_ENTRY *entry_from_index(PATH_ENTRY *entry, u16 index) {
    if (entry->index == index) return entry;
    u32 i;
    for (i = 0; i < entry->childCount; i++) {
        PATH_ENTRY *match = entry_from_index(&entry->children[i], index);
        if (match) return match;
    }
    return NULL;
}

static PATH_ENTRY *add_child_entry(PATH_ENTRY *dir) {
    PATH_ENTRY *newChildren = realloc(dir->children, (dir->childCount + 1) * sizeof(PATH_ENTRY));
    if (!newChildren) return NULL;
    bzero(newChildren + dir->childCount, sizeof(PATH_ENTRY));
    dir->children = newChildren;
    PATH_ENTRY *child = &dir->children[dir->childCount++];
    return child;
}

static bool read_directories() {
    VOLUME_DESCRIPTOR *volume = read_volume_descriptor(2);
    if (volume) unicode = true;
    else if (!(volume = read_volume_descriptor(1))) return false;

    if (!(root = malloc(sizeof(PATH_ENTRY)))) return false;
    bzero(root, sizeof(PATH_ENTRY));
    root->table_entry.name_length = 1;
    root->table_entry.extended_sectors = volume->root[OFFSET_EXTENDED];
    root->table_entry.sector = *(u32 *)(volume->root + OFFSET_SECTOR);
    root->table_entry.parent = 0;
    root->table_entry.name[0] = '\x00';
    root->index = 1;
    current = root;

    u32 path_table = volume->path_table_be;
    u32 path_table_len = volume->path_table_len_be;
    u16 i = 1;
    u64 offset = sizeof(PATHTABLE_ENTRY) - ISO_MAXPATHLEN + 2;
    PATH_ENTRY *parent = root;
    while (i < 0xffff && offset < path_table_len) {
        PATHTABLE_ENTRY entry;
        if (_read(&entry, (u64)path_table * SECTOR_SIZE + offset, sizeof(PATHTABLE_ENTRY)) != sizeof(PATHTABLE_ENTRY)) return false; // kinda dodgy - could be reading too far
        if (parent->index != entry.parent) parent = entry_from_index(root, entry.parent);
        if (!parent) return false;
        PATH_ENTRY *child = add_child_entry(parent);
        if (!child) return false;
        memcpy(&child->table_entry, &entry, sizeof(PATHTABLE_ENTRY));
        offset += sizeof(PATHTABLE_ENTRY) - ISO_MAXPATHLEN + child->table_entry.name_length;
        if (child->table_entry.name_length % 2) offset++;
        child->index = ++i;

        if (unicode) {
            u32 i;
            for (i = 0; i < (child->table_entry.name_length / 2); i++) child->table_entry.name[i] = entry.name[i * 2 + 1];
            child->table_entry.name[i] = '\x00';
            child->table_entry.name_length = i;
        } else {
            child->table_entry.name[child->table_entry.name_length] = '\x00';
        }

    }

    return true;
}

static void cleanup_recursive(PATH_ENTRY *entry) {
    u32 i;
    for (i = 0; i < entry->childCount; i++)
        cleanup_recursive(&entry->children[i]);
    if (entry->children) free(entry->children);
}

bool ISO9660_Mount() {
    ISO9660_Unmount();
    bool success = read_directories() && (dotab_device = AddDevice(&dotab_iso9660)) >= 0;
    if (success) last_access = gettime();
    else ISO9660_Unmount();
    return success;
}

bool ISO9660_Unmount() {
    if (root) {
        cleanup_recursive(root);
        free(root);
        root = NULL;
    }
    current = root;
    unicode = false;
    cache_sectors = 0;
    last_access = 0;
    if (dotab_device >= 0) {
        dotab_device = -1;
        return !RemoveDevice(DEVICE_NAME ":/");
    }
    return true;
}

u64 ISO9660_LastAccess() {
    return last_access;
}
