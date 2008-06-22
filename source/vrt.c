/*

Copyright (C) 2008 Joseph Jordan <joe.ftpii@psychlaw.com.au>
This work is derived from Daniel Ehlers' <danielehlers@mindeye.net> srg_vrt branch.

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
#include <malloc.h>
#include <stdarg.h>
#include <string.h>
#include <sys/dir.h>
#include <unistd.h>

#include "common.h"

static const char *VIRTUAL_PARTITION_ALIASES[] = { "/gc1", "/gc2", "/sd", "/usb" };
static const u32 MAX_VIRTUAL_PARTITION_ALIASES = (sizeof(VIRTUAL_PARTITION_ALIASES) / sizeof(char *));
static const u32 VRT_DEVICE_ID = 38744;

/*
    Converts a real absolute path to a client-visible path
    E.g. "fat3:/foo" -> "/sd/foo"
    The resulting path will fit into an array of size MAXPATHLEN
    For this reason, a virtual prefix (e.g. "/sd" or "/usb" cannot be longer than "fatX:", i.e. 5 characters)
    This function will not fail.  If it cannot complete successfully, the program will terminate with an error message.
*/
char *to_virtual_path(char *real_path) {
    const char *alias = NULL;
    u32 i;
    for (i = 0; i < MAX_VIRTUAL_PARTITION_ALIASES; i++) {
        if (!strncasecmp("fat", real_path, 3) && real_path[3] == ('1' + i) && real_path[4] == ':') {
            alias = VIRTUAL_PARTITION_ALIASES[i];
            real_path += 5;
        }
    }
    if (!alias) {
        die("FATAL: BUG: Unable to convert real path to client-visible path, exiting");
    }
    
    size_t virtual_path_size = strlen(alias) + strlen(real_path) + 1;
    if (virtual_path_size > MAXPATHLEN) {
        die("FATAL: BUG: Client-visible representation of real path is longer than MAXPATHLEN, exiting");
    }

    char *path = malloc(virtual_path_size);
    if (!path) {
        die("FATAL: Unable to allocate memory for client-visible path, exiting");
    }
    strcpy(path, alias);
    strcat(path, real_path);
    return path;
}

static char *virtual_abspath(char *virtual_cwd, char *virtual_path) {
    char *path;
    if (virtual_path[0] == '/') {
        path = virtual_path;
    } else {
        size_t path_size = strlen(virtual_cwd) + strlen(virtual_path) + 1;
        if (path_size > MAXPATHLEN || !(path = malloc(path_size))) return NULL;
        strcpy(path, virtual_cwd);
        strcat(path, virtual_path);
    }
    
    char *normalised_path = malloc(strlen(path) + 1);
    if (!normalised_path) goto end;
    *normalised_path = '\0';
    char *curr_dir = normalised_path;

    u32 state = 0; // 0:start, 1:slash, 2:dot, 3:dotdot
    char *token = path;
    while (1) {
        switch (state) {
        case 0:
            if (*token == '/') {
                state = 1;
                curr_dir = normalised_path + strlen(normalised_path);
                strncat(normalised_path, token, 1);
            }
            break;
        case 1:
            if (*token == '.') state = 2;
            else if (*token != '/') state = 0;
            break;
        case 2:
            if (*token == '/' || !*token) {
                state = 1;
                *(curr_dir + 1) = '\0';
            } else if (*token == '.') state = 3;
            else state = 0;
            break;
        case 3:
            if (*token == '/' || !*token) {
                state = 1;
                *curr_dir = '\0';
                char *prev_dir = strrchr(normalised_path, '/');
                if (prev_dir) curr_dir = prev_dir;
                else *curr_dir = '/';
                *(curr_dir + 1) = '\0';
            } else state = 0;
            break;
        }
        if (!*token) break;
        if (state == 0 || *token != '/') strncat(normalised_path, token, 1);
        token++;
    }

    end:
    if (path != virtual_path) free(path);
    return normalised_path;
}

/*
    Converts a client-visible path to a real absolute path
    E.g. "/sd/foo"    -> "fat3:/foo"
         "/sd"        -> "fat3:/"
         "/sd/../usb" -> "fat4:/"
    The resulting path will fit in an array of size MAXPATHLEN
    Returns NULL to indicate that the client-visible path is invalid
*/
char *to_real_path(char *virtual_cwd, char *virtual_path) {
    if (strchr(virtual_path, ':')) {
        // TODO: set ENOENT error
        return NULL; // colon is not allowed in virtual path, i've decided =P
    }

    virtual_path = virtual_abspath(virtual_cwd, virtual_path);
    if (!virtual_path) return NULL;

    char *path = NULL;
    char *rest = virtual_path;

    if (!strcmp("/", virtual_path)) {
        // indicate vfs-root with ""
        path = "";
        goto end;
    }

    char prefix[7] = { '\0' };
    u32 i;
    for (i = 0; i < MAX_VIRTUAL_PARTITION_ALIASES; i++) {
        const char *alias = VIRTUAL_PARTITION_ALIASES[i];
        size_t alias_len = strlen(alias);
        if (!strcasecmp(alias, virtual_path) || (!strncasecmp(alias, virtual_path, alias_len) && virtual_path[alias_len] == '/')) {
            sprintf(prefix, "fat%i:/", i + 1);
            rest += alias_len;
            if (*rest == '/') rest++;
            break;
        }
    }
    if (!*prefix) goto end; // TODO: set ENODEV error
    
    size_t real_path_size = strlen(prefix) + strlen(rest) + 1;
    if (real_path_size > MAXPATHLEN) goto end; // TODO: set ENOENT error

    path = malloc(real_path_size);
    if (!path) goto end;
    strcpy(path, prefix);
    strcat(path, rest);

    end:
    free(virtual_path);
    return path;
}

typedef void * (*path_func)(char *path, ...);

void *with_virtual_path(void *virtual_cwd, void *void_f, char *virtual_path, s32 failed, ...) {
    char *path = to_real_path(virtual_cwd, virtual_path);
    if (!path || !*path) return (void *)failed;
    
    path_func f = (path_func)void_f;
    va_list ap;
    void *args[3];
    unsigned int num_args = 0;
    va_start(ap, failed);
    do {
        void *arg = va_arg(ap, void *);
        if (!arg) break;
        args[num_args++] = arg;
    } while (1);
    va_end(ap);
    
    void *result;
    switch (num_args) {
        case 0: result = f(path); break;
        case 1: result = f(path, args[0]); break;
        case 2: result = f(path, args[0], args[1]); break;
        case 3: result = f(path, args[0], args[1], args[2]); break;
        default: result = (void *)failed;
    }
    
    free(path);
    return result;
}

FILE *vrt_fopen(char *cwd, char *path, char *mode) {
    return with_virtual_path(cwd, fopen, path, 0, mode, NULL);
}

int vrt_stat(char *cwd, char *path, struct stat *st) {
    // TODO: VFS: Handle vfs-root
    return (int)with_virtual_path(cwd, stat, path, -1, st, NULL);
}

static char *vrt_getcwd(char *buf, size_t size) {
    char real_path[size];
    if (!getcwd(real_path, size)) return NULL;
    char *virtual_path = to_virtual_path(real_path);
    if (strlen(virtual_path) >= size) {
        free(virtual_path);
        return NULL;
    }
    strcpy(buf, virtual_path);
    return buf;
}

int vrt_chdir(char *cwd, char *path) {
    char *real_path = to_real_path(cwd, path);
    if (!real_path) return -1;
    else if (!*real_path) {
        strcpy(cwd, "/");
        return 0;
    }
    free(real_path);
    mutex_acquire();
    int result = (int)with_virtual_path(cwd, chdir, path, -1, NULL);
    if (!result) vrt_getcwd(cwd, MAXPATHLEN); // TODO: error checking
    mutex_release();
    return result;
}

int vrt_unlink(char *cwd, char *path) {
    return (int)with_virtual_path(cwd, unlink, path, -1, NULL);
}

int vrt_mkdir(char *cwd, char *path, mode_t mode) {
    return (int)with_virtual_path(cwd, mkdir, path, -1, mode, NULL);
}

int vrt_rename(char *cwd, char *from_path, char *to_path) {
    char *real_to_path = to_real_path(cwd, to_path);
    if (!real_to_path || !*real_to_path) return -1;
    int result = (int)with_virtual_path(cwd, rename, from_path, -1, real_to_path, NULL);
    free(real_to_path);
    return result;
}

/*
    When in vfs-root this creates a fake DIR_ITER.
 */
DIR_ITER *vrt_diropen(char *cwd, char *path) {
    char *real_path = to_real_path(cwd, path);
    if (!real_path) return NULL; // TODO: set ENOENT error
    else if (!*real_path) {
        DIR_ITER *iter = malloc(sizeof(DIR_ITER));
        if (!iter) return NULL;
        iter->device = VRT_DEVICE_ID;
        iter->dirStruct = 0;
        return iter;
    }
    free(real_path);
    return with_virtual_path(cwd, diropen, path, 0, NULL);
}

/*
    Yields virtual aliases when iter->device == VRT_DEVICE_ID.
 */
int vrt_dirnext(DIR_ITER *iter, char *filename, struct stat *st) {
    if (iter->device == VRT_DEVICE_ID) {
        for (; (int)iter->dirStruct < MAX_VIRTUAL_PARTITION_ALIASES; iter->dirStruct++) {
            char prefix[7];
            sprintf(prefix, "fat%i:/", (int)iter->dirStruct + 1);
            DIR_ITER *prefix_iter = diropen(prefix);
            if (prefix_iter) {
                dirclose(prefix_iter);
                st->st_mode = S_IFDIR;
                st->st_size = 0;
                strcpy(filename, VIRTUAL_PARTITION_ALIASES[(int)iter->dirStruct] + 1);
                iter->dirStruct++;
                return 0;
            }
        }
        return -1;
    }
    return dirnext(iter, filename, st);
}

int vrt_dirclose(DIR_ITER *iter) {
    if (iter->device == VRT_DEVICE_ID) {
        free(iter);
        return 0;
    }
    return dirclose(iter);
}
