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
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/iosupport.h>

#include "fst.h"

#define FLAG_DIR 1
#define DIR_SEPARATOR '/'
#define SECTOR_SIZE 0x800
#define BUFFER_SIZE 0x8000

typedef struct DIR_ENTRY_STRUCT {
    char name[FST_MAXPATHLEN];
	u32 partition_offset;
    u32 offset; // for files this is the offset of the file payload in the disc partition, for directories it is the index of the entry in the fst
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

static unsigned char read_buffer[BUFFER_SIZE] __attribute__((aligned(32)));

static DIR_ENTRY *root = NULL;
static DIR_ENTRY *current = NULL;

static bool is_dir(DIR_ENTRY *entry) {
	return entry->flags & FLAG_DIR;
}

static DIR_ENTRY *entry_from_path(const char *path) {
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
    u32 end_sector = (offset + len - 1) / SECTOR_SIZE;
    u32 sectors = end_sector - sector + 1;
	if (DI_ReadDVD(read_buffer, sectors, sector)) return -1;
    memcpy(ptr, read_buffer + sector_offset, len);
    return len;
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

	u32 real_offset = file->entry->offset + file->offset;
	u32 read_offset = real_offset / 4 << 2;
	u32 buffer_offset = real_offset % 4;
    len = MIN(BUFFER_SIZE - buffer_offset, len);
	if (DI_OpenPartition(file->entry->partition_offset) || DI_Read(read_buffer, len, read_offset) || DI_ClosePartition()) {
		r->_errno = EIO;
		return -1;
	}
    memcpy(ptr, read_buffer + buffer_offset, len);
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

static int _FST_statvfs_r(struct _reent *r, const char *path, struct statvfs *buf) {
    r->_errno = ENOTSUP;
    return -1;
}

static int _FST_write_r(struct _reent *r, int fd, const char *ptr, int len) {
    r->_errno = EBADF;
    return -1;
}

static int _FST_link_r(struct _reent *r, const char *existing, const char *newLink) {
    r->_errno = EROFS;
    return -1;
}

static int _FST_unlink_r(struct _reent *r, const char *path) {
    r->_errno = EROFS;
    return -1;
}

static int _FST_rename_r(struct _reent *r, const char *oldName, const char *newName) {
    r->_errno = EROFS;
    return -1;
}

static int _FST_mkdir_r(struct _reent *r, const char *path, int mode) {
    r->_errno = EROFS;
    return -1;
}

static const devoptab_t dotab_fst = {
    "fst",
    sizeof(FILE_STRUCT),
    _FST_open_r,
    _FST_close_r,
    _FST_write_r,
    _FST_read_r,
    _FST_seek_r,
    _FST_fstat_r,
    _FST_stat_r,
    _FST_link_r,
    _FST_unlink_r,
    _FST_chdir_r,
    _FST_rename_r,
    _FST_mkdir_r,
    sizeof(DIR_STATE_STRUCT),
    _FST_diropen_r,
    _FST_dirreset_r,
    _FST_dirnext_r,
    _FST_dirclose_r,
    _FST_statvfs_r
};

struct disc_header {
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

struct partition_info {
	u32 count;
	u32 table_offset;
} __attribute__((packed));

struct partition_table_entry {
	u32 offset;
	u32 type;
} __attribute__((packed));

struct fst_info {
	u32 dol_offset;
	u32 fst_offset;
	u32 fst_size;
	u32 fst_size2;
} __attribute__ ((packed));

typedef struct {
	u8 filetype;
	char name_offset[3];
	u32 fileoffset;	// 	file_offset or parent_offset (dir)
	u32 filelen;	// 	file_length or num_entries (root) or next_offset (dir)
} __attribute__((packed)) FST;

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
	child->partition_offset = dir->partition_offset;
	return child;
}

static bool read_partition(DIR_ENTRY *partition, u32 fst_offset) {
	if (DI_Read(read_buffer, sizeof(FST), fst_offset)) return false;
	FST *fst = (FST *)read_buffer;
	u32 no_fst_entries = fst->filelen;
	u32 name_table_offset = no_fst_entries * sizeof(FST);
	char *name_table = (char *)read_buffer + name_table_offset;
	if (DI_Read(read_buffer, BUFFER_SIZE, fst_offset)) return false; // TODO: make sure to read all of it...
	DIR_ENTRY *current_dir = partition;
	u32 i;
	for (i = 1; i < no_fst_entries; i++) {
		fst++;
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

static bool read_fst() {
	if (DI_ReadDVD(read_buffer, 1, 128)) return false;
	struct partition_info infos[4];
	memcpy(infos, read_buffer, sizeof(struct partition_info) * 4);
	u32 table_index;
	u32 partition_number = 0;

	if (!(root = malloc(sizeof(DIR_ENTRY)))) return false;
	bzero(root, sizeof(DIR_ENTRY));
	root->flags = FLAG_DIR;
	current = root;

	for (table_index = 0; table_index < 4; table_index++) {
		u32 count = infos[table_index].count;
		if (count > 0) {
			struct partition_table_entry entries[count];
			if (_read(entries, (u64)infos[table_index].table_offset << 2, sizeof(struct partition_table_entry) * count) < 0) return false; // read table entries
			u32 partition_index;
			for (partition_index = 0; partition_index < count; partition_index++) {
				DIR_ENTRY *partition = add_child_entry(root);
				if (!partition) return false;
				sprintf(partition->name, "%u", partition_number);
				partition->flags = FLAG_DIR;
				partition->partition_offset = entries[partition_index].offset;
				if (DI_OpenPartition(partition->partition_offset)) return false;
				if (DI_Read(read_buffer, sizeof(struct fst_info), 0x420 >> 2)) return false;
				struct fst_info *fst_info = (struct fst_info *)read_buffer;
				if (!read_partition(partition, fst_info->fst_offset)) return false;
				if (DI_ClosePartition()) return false;
				partition_number++;
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
    bool success = read_fst() && AddDevice(&dotab_fst) >= 0;
    if (!success) FST_Unmount();
    return success;
}

bool FST_Unmount() {
	if (root) {
		cleanup_recursive(root);
		free(root);
		root = NULL;
	}
	current = root;
    return !RemoveDevice("fst:/");
}
