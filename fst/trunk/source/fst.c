/*

libfst -- a Wii disc filesystem devoptab library for the Wii

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
#include <ogc/es.h>
#include <ogc/lwp_watchdog.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/iosupport.h>

#include "fst.h"
#include "rijndael.h"

#define DEVICE_NAME "fst"

#define FLAG_DIR 1
#define DIR_SEPARATOR '/'
#define SECTOR_SIZE 0x800
#define BUFFER_SIZE 0x8000

#define CLUSTER_HEADER_SIZE 0x400
#define ENCRYPTED_CLUSTER_SIZE 0x8000
#define PLAINTEXT_CLUSTER_SIZE (ENCRYPTED_CLUSTER_SIZE - CLUSTER_HEADER_SIZE)
#define AES_BLOCK_SIZE 16

typedef struct {
    u32 dol_offset;
    u32 fst_offset;
    u32 fst_size;
    u32 fst_size2;
} __attribute__ ((packed)) FST_INFO;

typedef struct {
    u32 offset;
    aeskey key;
    FST_INFO fst_info;
} PARTITION;

typedef struct DIR_ENTRY_STRUCT {
    char name[FST_MAXPATHLEN];
    u32 partition;
    u32 offset; // offset in partition >> 2, or fst index for dirs
    u32 size;
    u8 flags;
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
static u8 cluster_buffer[ENCRYPTED_CLUSTER_SIZE] __attribute__((aligned(32)));

static DIR_ENTRY *root = NULL;
static DIR_ENTRY *current = NULL;
static PARTITION *partitions = NULL;
static u64 last_access = 0;
static s32 dotab_device = -1;

static u8 aescache[PLAINTEXT_CLUSTER_SIZE] __attribute__((aligned(32)));
static u64 aescache_start = 0;
static u64 aescache_end = 0;

static bool is_dir(DIR_ENTRY *entry) {
    return entry->flags & FLAG_DIR;
}

static bool invalid_drive_specifier(const char *path) {
    if (strchr(path, ':') == NULL) return false;
	int namelen = strlen(DEVICE_NAME);
    if (!strncmp(DEVICE_NAME, path, namelen) && path[namelen] == ':') return false;
    return true;
}

static DIR_ENTRY *entry_from_path(const char *path) {
    if (invalid_drive_specifier(path)) return NULL;
    if (strchr(path, ':') != NULL) path = strchr(path, ':') + 1;
    DIR_ENTRY *entry;
    bool found = false;
    bool notFound = false;
    const char *pathPosition = path;
    const char *pathEnd = strchr(path, '\0');
    if (pathPosition[0] == DIR_SEPARATOR) {
        entry = root;
        while (pathPosition[0] == DIR_SEPARATOR) pathPosition++;
        if (pathPosition >= pathEnd) found = true;
    } else {
        entry = current;
    }
    if (entry == root && !strcmp(".", pathPosition)) found = true;
    DIR_ENTRY *dir = entry;
    while (!found && !notFound) {
        const char *nextPathPosition = strchr(pathPosition, DIR_SEPARATOR);
        size_t dirnameLength;
        if (nextPathPosition != NULL) dirnameLength = nextPathPosition - pathPosition;
        else dirnameLength = strlen(pathPosition);
        if (dirnameLength >= FST_MAXPATHLEN) return NULL;

        u32 fileIndex = 0;
        while (fileIndex < dir->fileCount && !found && !notFound) {
            entry = &dir->children[fileIndex];
            if (dirnameLength == strnlen(entry->name, FST_MAXPATHLEN - 1) && !strncasecmp(pathPosition, entry->name, dirnameLength)) found = true;
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

static int _FST_open_r(struct _reent *r, void *fileStruct, const char *path, int flags, int mode) {
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

static int _FST_close_r(struct _reent *r, int fd) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    file->inUse = false;
    return 0;
}

static int _read(void *ptr, u64 offset, u32 len) {
    u32 sector = offset / SECTOR_SIZE;
    u32 sector_offset = offset % SECTOR_SIZE;
    len = MIN(BUFFER_SIZE - sector_offset, len);
    if (DI_ReadDVD(read_buffer, BUFFER_SIZE / SECTOR_SIZE, sector)) {
        last_access = gettime();
        return -1;
    }
    last_access = gettime();
    memcpy(ptr, read_buffer + sector_offset, len);
    return len;
}

static u64 cipher_to_plaintext(u64 offset) {
    return offset / ENCRYPTED_CLUSTER_SIZE * PLAINTEXT_CLUSTER_SIZE + (offset % ENCRYPTED_CLUSTER_SIZE) - CLUSTER_HEADER_SIZE;
}

static u64 plaintext_to_cipher(u64 offset) {
    return offset / PLAINTEXT_CLUSTER_SIZE * ENCRYPTED_CLUSTER_SIZE + (offset % PLAINTEXT_CLUSTER_SIZE) + CLUSTER_HEADER_SIZE;
}

static bool read_and_decrypt_cluster(aeskey title_key, u8 *buf, u64 offset, u32 offset_from_cluster, u32 len) {
    u64 cache_start = cipher_to_plaintext(offset + offset_from_cluster);
    u64 cache_end = cipher_to_plaintext(offset + offset_from_cluster + len);
    if (aescache_end && cache_start >= aescache_start && cache_end <= aescache_end) {
        memcpy(buf, aescache + (cache_start - aescache_start), len);
        return true;
    }
    u32 bytes_read = _read(buf, offset, ENCRYPTED_CLUSTER_SIZE);
    if (bytes_read != ENCRYPTED_CLUSTER_SIZE) return false;
    u8 *iv = buf + 0x3d0;
    u8 *inbuf = buf + CLUSTER_HEADER_SIZE;
    aes_set_key(title_key);
    aes_decrypt(iv, inbuf, aescache, PLAINTEXT_CLUSTER_SIZE);
    aescache_start = cipher_to_plaintext(offset + CLUSTER_HEADER_SIZE);
    aescache_end = aescache_start + PLAINTEXT_CLUSTER_SIZE;
    memcpy(buf, aescache + offset_from_cluster - CLUSTER_HEADER_SIZE, len);
    return true;
}

static int _FST_read_r(struct _reent *r, int fd, char *ptr, int len) {
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

    u64 offset_from_data = plaintext_to_cipher((file->entry->offset << 2LL) + file->offset);
    u64 cluster_offset_from_data = (offset_from_data / ENCRYPTED_CLUSTER_SIZE) * ENCRYPTED_CLUSTER_SIZE;
    u32 offset_from_cluster = offset_from_data % ENCRYPTED_CLUSTER_SIZE;
    len = MIN(ENCRYPTED_CLUSTER_SIZE - offset_from_cluster, len);
    u64 data_offset = (partitions[file->entry->partition].offset << 2LL) + 0x20000;
    if (!read_and_decrypt_cluster(partitions[file->entry->partition].key, cluster_buffer, data_offset + cluster_offset_from_data, offset_from_cluster, len)) {
        r->_errno = EIO;
        return -1;
    }
    memcpy(ptr, cluster_buffer, len);
    file->offset += len;
    return len;
}

static int _FST_seek_r(struct _reent *r, int fd, int pos, int dir) {
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
    st->st_dev = 0x4657;
    st->st_ino = 0;
    st->st_mode = ((is_dir(entry)) ? S_IFDIR : S_IFREG) | (S_IRUSR | S_IRGRP | S_IROTH);
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

static int _FST_fstat_r(struct _reent *r, int fd, struct stat *st) {
    FILE_STRUCT *file = (FILE_STRUCT *)fd;
    if (!file->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    stat_entry(file->entry, st);
    return 0;
}

static int _FST_stat_r(struct _reent *r, const char *path, struct stat *st) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    }
    stat_entry(entry, st);
    return 0;
}

static int _FST_chdir_r(struct _reent *r, const char *path) {
    DIR_ENTRY *entry = entry_from_path(path);
    if (!entry) {
        r->_errno = ENOENT;
        return -1;
    } else if (!is_dir(entry)) {
        r->_errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static DIR_ITER *_FST_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path) {
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

static int _FST_dirreset_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->index = 0;
    return 0;
}

static int _FST_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st) {
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
    strncpy(filename, entry->name, FST_MAXPATHLEN - 1);
    stat_entry(entry, st);
    return 0;
}

static int _FST_dirclose_r(struct _reent *r, DIR_ITER *dirState) {
    DIR_STATE_STRUCT *state = (DIR_STATE_STRUCT *)(dirState->dirStruct);
    if (!state->inUse) {
        r->_errno = EBADF;
        return -1;
    }
    state->inUse = false;
    return 0;
}

static const devoptab_t dotab_fst = {
    DEVICE_NAME,
    sizeof(FILE_STRUCT),
    _FST_open_r,
    _FST_close_r,
    NULL,
    _FST_read_r,
    _FST_seek_r,
    _FST_fstat_r,
    _FST_stat_r,
    NULL,
    NULL,
    _FST_chdir_r,
    NULL,
    NULL,
    sizeof(DIR_STATE_STRUCT),
    _FST_diropen_r,
    _FST_dirreset_r,
    _FST_dirnext_r,
    _FST_dirclose_r,
    NULL
};

typedef struct {
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
} __attribute__((packed)) DISC_HEADER;

typedef struct {
    u32 count;
    u32 table_offset;
} __attribute__((packed)) PARTITION_TABLE_ENTRY;

typedef struct {
    u32 offset;
    u32 type;
} __attribute__((packed)) PARTITION_ENTRY;

typedef struct {
    u8 filetype;
    char name_offset[3];
    u32 fileoffset;
    u32 filelen;
} __attribute__((packed)) FST_ENTRY;

static DIR_ENTRY *entry_from_index(DIR_ENTRY *entry, u32 index) {
    if (entry->offset == index) return entry;
    u32 i;
    for (i = 0; i < entry->fileCount; i++) {
        if (is_dir(&entry->children[i])) {
            DIR_ENTRY *match = entry_from_index(&entry->children[i], index);
            if (match) return match;
        }
    }
    return NULL;
}

static DIR_ENTRY *add_child_entry(DIR_ENTRY *dir) {
    DIR_ENTRY *newChildren = realloc(dir->children, (dir->fileCount + 1) * sizeof(DIR_ENTRY));
    if (!newChildren) return NULL;
    bzero(newChildren + dir->fileCount, sizeof(DIR_ENTRY));
    dir->children = newChildren;
    DIR_ENTRY *child = &dir->children[dir->fileCount++];
    child->partition = dir->partition;
    return child;
}

static bool read_partition(DIR_ENTRY *partition, u32 fst_offset) {
    if (DI_Read(read_buffer, sizeof(FST_ENTRY), fst_offset)) return false;
    FST_ENTRY *fst = (FST_ENTRY *)read_buffer;
    u32 no_fst_entries = fst->filelen;
    u32 name_table_offset = no_fst_entries * sizeof(FST_ENTRY);
    char *name_table = (char *)read_buffer + name_table_offset;
    if (DI_Read(read_buffer, BUFFER_SIZE, fst_offset)) return false;
    DIR_ENTRY *current_dir = partition;
    u32 i;
    for (i = 1; i < no_fst_entries; i++) {
        fst++;
        if ((u8 *)fst >= (read_buffer + BUFFER_SIZE)) {
            if (DI_Read(read_buffer, BUFFER_SIZE, fst_offset + (BUFFER_SIZE >> 2))) return false;
            fst = (FST_ENTRY *)read_buffer;
        }
        DIR_ENTRY *child;
        u32 name_offset = (fst->name_offset[0] << 16) | (fst->name_offset[1] << 8) | fst->name_offset[2];
        if (fst->filetype & FLAG_DIR) {
            DIR_ENTRY *parent = entry_from_index(partition, fst->fileoffset);
            if (!parent) return false;
            current_dir = child = add_child_entry(parent);
            if (!child) return false;
            child->offset = i;
        } else {
            child = add_child_entry(current_dir);
            if (!child) return false;
            child->offset = fst->fileoffset;
            child->size = fst->filelen;
        }
        child->flags = fst->filetype;
        strcpy(child->name, name_table + name_offset);
    }
    return true;
}

#define COMMON_AES_KEY ((u8 *)"\xeb\xe4\x2a\x22\x5e\x85\x93\xe4\x48\xd9\xc5\x45\x73\x81\xaa\xf7")
#define KOREAN_AES_KEY ((u8 *)"\x63\xb8\x2b\xb4\xf4\x61\x4e\x2e\x13\xf2\xfe\xfb\xba\x4c\x9b\x7e")
#define KOREAN_KEY_FLAG 11

static bool read_title_key(aeskey title_key, u32 partition_offset) {
    tik ticket;
    if (_read(&ticket, (partition_offset << 2) + 0x140, sizeof(tik)) != sizeof(tik)) return false;
    u8 iv[16];
    bzero(iv, 16);
    memcpy(iv, &ticket.titleid, sizeof(ticket.titleid));
    if (ticket.reserved[KOREAN_KEY_FLAG]) {
        aes_set_key(KOREAN_AES_KEY);
    } else {
        aes_set_key(COMMON_AES_KEY);
    }
    aes_decrypt(iv, ticket.cipher_title_key, title_key, 16);
    return true;
}

static bool read_disc() {
    if (DI_ReadDVD(read_buffer, 1, 0)) return false;
    DISC_HEADER *header = (DISC_HEADER *)read_buffer;
    if (memcmp(header->magic, "\x5d\x1c\x9e\xa3", 4)) return false;
    if (DI_ReadDVD(read_buffer, 1, 128)) return false;
    PARTITION_TABLE_ENTRY tables[4];
    memcpy(tables, read_buffer, sizeof(PARTITION_TABLE_ENTRY) * 4);
    u32 table_index;
    u32 partition_count = 0;

    if (!(root = malloc(sizeof(DIR_ENTRY)))) return false;
    bzero(root, sizeof(DIR_ENTRY));
    root->flags = FLAG_DIR;
    current = root;

    for (table_index = 0; table_index < 4; table_index++) {
        u32 count = tables[table_index].count;
        if (count > 0) {
            PARTITION_ENTRY entries[count];
            u32 table_size = sizeof(PARTITION_ENTRY) * count;
            if (_read(entries, (u64)tables[table_index].table_offset << 2, table_size) != table_size) return false;
            u32 partition_index;
            for (partition_index = 0; partition_index < count; partition_index++) {
                PARTITION *newPartitions = realloc(partitions, sizeof(PARTITION) * (partition_count + 1));
                if (!newPartitions) return false;
                partitions = newPartitions;
                bzero(partitions + partition_count, sizeof(PARTITION));
                PARTITION *partition = partitions + partition_count;
                DIR_ENTRY *partition_entry = add_child_entry(root);
                if (!partition_entry) return false;
                sprintf(partition_entry->name, "%u", partition_count);
                partition_entry->flags = FLAG_DIR;
                partition_entry->partition = partition_count;
                partition->offset = entries[partition_index].offset;
                if (!read_title_key(partition->key, partition->offset)) return false;

                if (DI_OpenPartition(partition->offset)) return false;
                if (DI_Read(read_buffer, sizeof(FST_INFO), 0x420 >> 2)) {
                    DI_ClosePartition();
                    return false;
                }
                memcpy(&partition->fst_info, read_buffer, sizeof(FST_INFO));
                if (!read_partition(partition_entry, partition->fst_info.fst_offset)) {
                    DI_ClosePartition();
                    return false;
                }
                if (DI_ClosePartition()) return false;
                partition_count++;
            }
        }
    }
    return true;
}

static void cleanup_recursive(DIR_ENTRY *entry) {
    u32 i;
    for (i = 0; i < entry->fileCount; i++)
        if (is_dir(&entry->children[i]))
            cleanup_recursive(&entry->children[i]);
    if (entry->children) free(entry->children);
}

bool FST_Mount() {
    FST_Unmount();
    bool success = read_disc() && (dotab_device = AddDevice(&dotab_fst)) >= 0;
    if (success) last_access = gettime();
    else FST_Unmount();
    return success;
}

bool FST_Unmount() {
    if (root) {
        cleanup_recursive(root);
        free(root);
        root = NULL;
    }
    if (partitions) {
        free(partitions);
        partitions = NULL;
    }
    current = root;
    aescache_end = 0;
    last_access = 0;
    if (dotab_device >= 0) {
        dotab_device = -1;
        return !RemoveDevice(DEVICE_NAME ":/");
    }
    return true;
}

u64 FST_LastAccess() {
    return last_access;
}
