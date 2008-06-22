/*

ftpii -- an FTP server for the Wii

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
#include <fat.h>
#include <malloc.h>
#include <network.h>
#include <ogcsys.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/dir.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

#include "common.h"

extern u32 net_gethostip();

#define BUFFER_SIZE 512

static const u16 SRC_PORT = 20;
static const u16 PORT = 21;
static const s32 EQUIT = 696969;
static const u8 MAX_CLIENTS = 5;
static const char *VIRTUAL_PARTITION_ALIASES[] = { "/gc1", "/gc2", "/sd", "/usb" };

struct client_struct {
    s32 socket;
    char representation_type;
    s32 passive_socket;
    char cwd[MAXPATHLEN];
    char pending_rename[MAXPATHLEN];
    long restart_marker;
    struct sockaddr_in address;
};

typedef struct client_struct client_t;

static volatile u8 num_clients = 0;
static volatile u16 passive_port = 1024;

static mutex_t global_mutex;
static mutex_t chdir_mutex;

/* for "debugging" */
// void wait_for_continue(char *msg) {
//     printf(msg);
//     printf("...hold B\n\n");
//     sleep(1);
//     while (!(WPAD_ButtonsHeld(0) & WPAD_BUTTON_B)) {
//         WPAD_ScanPads();
//         VIDEO_WaitVSync();
//     }
// }

/*
    TODO: support multi-line reply
*/
static s32 write_reply(client_t *client, u16 code, char *msg) {
    u32 msglen = 4 + strlen(msg) + CRLF_LENGTH;
    char msgbuf[msglen + 1];
    if (msgbuf == NULL) return -ENOMEM;
    sprintf(msgbuf, "%u %s\r\n", code, msg);
    printf("Wrote reply: %s", msgbuf);
    return write_exact(client->socket, msgbuf, msglen);
}

static char *virtual_abspath(char *virtual_path);

/*
    Converts a client-visible path to a real absolute path
    E.g. "/sd/foo"    -> "fat3:/foo"
         "/sd"        -> "fat3:/"
         "/sd/../usb" -> "fat4:/"
    The resulting path will fit in an array of size MAXPATHLEN
    Returns NULL to indicate that the client-visible path is invalid
*/
static char *to_real_path(char *virtual_path) {
    if (strchr(virtual_path, ':')) {
        // TODO: set ENOENT error
        return NULL; // colon is not allowed in virtual path, i've decided =P
    }

    virtual_path = virtual_abspath(virtual_path);
    if (!virtual_path) return NULL;

    char *path = NULL;
    char *rest = virtual_path;

    if (strcmp("/", virtual_path) == 0) goto end; // TODO: indicate virtual-root...

    char prefix[7] = { '\0' };
    u32 i;
    for (i = 0; i < (sizeof(VIRTUAL_PARTITION_ALIASES) / sizeof(char*)); i++) {
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

/*
    Converts a real absolute path to a client-visible path
    E.g. "fat3:/foo" -> "/sd/foo"
    The resulting path will fit into an array of size MAXPATHLEN
    For this reason, a virtual prefix (e.g. "/sd" or "/usb" cannot be longer than "fatX:", i.e. 5 characters)
    This function will not fail.  If it cannot complete successfully, the program will terminate with an error message.
*/
static char *to_virtual_path(char *real_path) {
    const char *alias = NULL;
    u32 i;
    for (i = 0; i < (sizeof(VIRTUAL_PARTITION_ALIASES) / sizeof(char*)); i++) {
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

static char *virtual_abspath(char *virtual_path) {
    char *path;
    if (virtual_path[0] == '/') {
        path = virtual_path;
    } else {
        // XXX: VFS: BUG: This is broken if client is currently in vfs-root
        char cwd[MAXPATHLEN];
        if (!getcwd(cwd, MAXPATHLEN)) return NULL;
        char *virtual_cwd = to_virtual_path(cwd);
        size_t path_size = strlen(virtual_cwd) + strlen(virtual_path) + 1;
        if (path_size > MAXPATHLEN || !(path = malloc(path_size))) {
            free(virtual_cwd);
            return NULL;
        }
        strcpy(path, virtual_cwd);
        strcat(path, virtual_path);
        free(virtual_cwd);
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

typedef void * (*path_func)(char *path, ...);

static void *with_virtual_path(void *void_f, char *virtual_path, s32 failed, ...) {
    char *path = to_real_path(virtual_path);
    if (!path) return (void *)failed;
    
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

/*
    Returns 0 on success
    On failure, the mutex is unlocked
*/
static s32 enter_cwd_context(client_t *client) {
    mutex_acquire(chdir_mutex);
    s32 result = chdir(client->cwd);
    if (result) mutex_release(chdir_mutex);
    return result;
}

/*
    Returns 0 on success
    On failure, the mutex is unlocked but the cwd is left in an unknown state.
    A thread must own the chdir_mutex (i.e. have previously called enter_cwd_context)
    before it is allowed to call this function.
*/
static s32 exit_cwd_context(client_t *client) {
    char *result = getcwd(client->cwd, MAXPATHLEN);
    mutex_release(chdir_mutex);
    return !result;
}

typedef s32 (*ftp_command_handler)(client_t *client, char *args);

s32 simple_cwd_context_handler(client_t *client, ftp_command_handler handler, char *rest) {
    if (enter_cwd_context(client)) return write_reply(client, 550, "Could not enter cwd context");
    s32 result = handler(client, rest);
    if (exit_cwd_context(client)) die("FATAL: Could not exit cwd context, exiting");
    return result;
}

static void close_passive_socket(client_t *client) {
    if (client->passive_socket >= 0) {
        net_close(client->passive_socket);
        client->passive_socket = -1;
    }
}

static s32 ftp_USER(client_t *client, char *username) {
    return write_reply(client, 331, "User name okay, need password.");
}

static s32 ftp_PASS(client_t *client, char *password) {
    return write_reply(client, 230, "User logged in, proceed.");
}

static s32 ftp_REIN(client_t *client, char *rest) {
    close_passive_socket(client);
    strcpy(client->cwd, "fat3:/"); // TODO: VFS: client should start in vfs-root
    client->representation_type = 'A';
    return write_reply(client, 220, "Service ready for new user.");
}

static s32 ftp_QUIT(client_t *client, char *rest) {
    // TODO: dont quit if xfer in progress
    s32 result = write_reply(client, 221, "Service closing control connection.");
    return result < 0 ? result : -EQUIT;
}

static s32 ftp_SYST(client_t *client, char *rest) {
    return write_reply(client, 215, "UNIX Type: L8 Version: ftpii");
}

static s32 ftp_TYPE(client_t *client, char *rest) {
    char representation_type[BUFFER_SIZE], param[BUFFER_SIZE];
    char *args[] = { representation_type, param };
    u32 num_args = split(rest, ' ', 1, args);
    if (num_args == 0) {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    } else if ((!strcasecmp("A", representation_type) && (!*param || !strcasecmp("N", param))) ||
               (!strcasecmp("I", representation_type) && num_args == 1)) {
        client->representation_type = *representation_type;
    } else {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    }
    char msg[15];
    sprintf(msg, "Type set to %s.", representation_type);
    return write_reply(client, 200, msg);
}

static s32 ftp_MODE(client_t *client, char *rest) {
    if (!strcasecmp("S", rest)) {
        return write_reply(client, 200, "Mode S ok.");
    } else {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    }
}

static s32 ftp_PWD(client_t *client, char *rest) {
    char msg[MAXPATHLEN + 24];
    // TODO: escape double-quotes
    // TODO: handle case where we're in vfs-root
    char *path = to_virtual_path(client->cwd);
    sprintf(msg, "\"%s\" is current directory.", path);
    free(path);
    return write_reply(client, 257, msg);
}

static s32 ftp_CWD(client_t *client, char *path) {
    // TODO: VFS: handle case where we should be changing to vfs-root
    if (!with_virtual_path(chdir, path, -1, NULL)) {
        return write_reply(client, 250, "CWD command successful.");
    } else  {
        return write_reply(client, 550, strerror(errno));
    }
}

static s32 ftp_CDUP(client_t *client, char *rest) {
    // TODO: VFS: set client cwd to vfs-root if we're in real-root
    if (!chdir("..")) {
        return write_reply(client, 250, "CDUP command successful.");
    } else  {
        return write_reply(client, 550, strerror(errno));
    }
}

static s32 ftp_DELE(client_t *client, char *path) {
    if (!with_virtual_path(unlink, path, -1, NULL)) {
        return write_reply(client, 250, "File or directory removed.");
    } else {
        return write_reply(client, 550, strerror(errno));
    }
}

static s32 ftp_MKD(client_t *client, char *path) {
    if (!*path) {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    }
    if (!with_virtual_path(mkdir, path, -1, 0777, NULL)) {
        // TODO: error-handling =P
        char new_path[MAXPATHLEN];
        chdir(path);
        getcwd(new_path, MAXPATHLEN);
        chdir(client->cwd);
        char msg[MAXPATHLEN + 21];
        // TODO: escape double-quotes
        char *virtual_path = to_virtual_path(new_path);
        sprintf(msg, "\"%s\" directory created.", virtual_path);
        free(virtual_path);
        return write_reply(client, 257, msg);
    } else {
        return write_reply(client, 550, strerror(errno));
    }
}

static s32 ftp_RNFR(client_t *client, char *path) {
    strcpy(client->pending_rename, path);
    return write_reply(client, 350, "Ready for RNTO.");
}

static s32 ftp_RNTO(client_t *client, char *path) {
    if (!*client->pending_rename) {
        return write_reply(client, 503, "RNFR required first.");
    }
    s32 result;
    char *real_path = to_real_path(path);
    if (real_path && !with_virtual_path(rename, client->pending_rename, -1, real_path, NULL)) {
        result = write_reply(client, 250, "Rename successful.");
    } else {
        result = write_reply(client, 550, strerror(errno));
    }
    if (real_path) {
        free(real_path);
    }
    *client->pending_rename = '\0';
    return result;
}

static s32 ftp_SIZE(client_t *client, char *path) {
    struct stat st;
    // TODO: VFS: handle case with path is vfs-root
    if (!with_virtual_path(stat, path, -1, &st, NULL)) {
        char size_buf[12];
        sprintf(size_buf, "%li", st.st_size); // XXX: what does this do for files over 2GB?
        return write_reply(client, 213, size_buf);
    } else {
        return write_reply(client, 550, strerror(errno));
    }
}

static s32 ftp_PASV(client_t *client, char *rest) {
    close_passive_socket(client);
    client->passive_socket = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (client->passive_socket < 0) {
        return write_reply(client, 520, "Unable to create listening socket.");
    }
    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family = AF_INET;
    mutex_acquire(global_mutex);
    bindAddress.sin_port = htons(passive_port++); // XXX: BUG: This will overflow eventually, with interesting results...
    mutex_release(global_mutex);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    s32 result;
    if ((result = net_bind(client->passive_socket, (struct sockaddr *)&bindAddress, sizeof(bindAddress))) < 0) {
        close_passive_socket(client);
        return write_reply(client, 520, "Unable to bind listening socket.");
    }
    if ((result = net_listen(client->passive_socket, 1)) < 0) {
        close_passive_socket(client);
        return write_reply(client, 520, "Unable to listen on socket.");
    }
    char reply[49];
    u16 port = bindAddress.sin_port;
    u32 ip = net_gethostip();
    printf("Listening for data connections at %s:%u...\n", inet_ntoa(*(struct in_addr *)&ip), port);
    sprintf(reply, "Entering Passive Mode (%u,%u,%u,%u,%u,%u).", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, (port >> 8) & 0xff, port & 0xff);
    return write_reply(client, 227, reply);
}

static s32 ftp_PORT(client_t *client, char *portspec) {
    u32 h1, h2, h3, h4, p1, p2;
    if (sscanf(portspec, "%3u,%3u,%3u,%3u,%3u,%3u", &h1, &h2, &h3, &h4, &p1, &p2) < 6) {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    }
    char addr_str[44];
    sprintf(addr_str, "%u.%u.%u.%u", h1, h2, h3, h4);
    struct in_addr sin_addr;
    if (!inet_aton(addr_str, &sin_addr)) {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    }
    close_passive_socket(client);
    u16 port = ((p1 &0xff) << 8) | (p2 & 0xff);
    client->address.sin_addr = sin_addr;
    client->address.sin_port = htons(port);
    printf("Set client address to %s:%u\n", addr_str, port);
    return write_reply(client, 200, "PORT command successful.");
}

typedef s32 (*data_connection_callback)(s32 data_socket, void *arg);
typedef s32 (*data_connection_handler)(client_t *client, data_connection_callback callback, void *arg);

static s32 do_data_connection_active(client_t *client, data_connection_callback callback, void *arg) {
    s32 data_socket = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (data_socket < 0) {
        printf("DEBUG: Unable to create data socket: [%i] %s\n", -data_socket, strerror(-data_socket));
        return data_socket;
    }
    printf("Attempting to connect to client at %s:%u\n", inet_ntoa(client->address.sin_addr), ntohs(client->address.sin_port));
    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(SRC_PORT);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    s32 result;
    if ((result = net_bind(data_socket, (struct sockaddr *)&bindAddress, sizeof(bindAddress))) < 0) {
        printf("DEBUG: Unable to bind data socket: [%i] %s\n", -result, strerror(-result));
        net_close(data_socket);
        return result;
    }
    if ((result = net_connect(data_socket, (struct sockaddr *)&client->address, sizeof(client->address))) < 0) {
        printf("Unable to connect to client: [%i] %s\n", -result, strerror(-result));
        net_close(data_socket);
        return result;
    }
    printf("Connected to client!  Transferring data...\n");

    result = callback(data_socket, arg);

    net_close(data_socket);
    if (result < 0) {
        printf("Error occurred while transferring data...\n");
    } else {
        printf("Finished transferring data!\n");
    }
    return result;
}

static s32 do_data_connection_passive(client_t *client, data_connection_callback callback, void *arg) {
    struct sockaddr_in data_peer_address;
    socklen_t addrlen = sizeof(data_peer_address);
    printf("Waiting for data connections...\n");
    s32 data_socket = net_accept(client->passive_socket, (struct sockaddr *)&data_peer_address ,&addrlen);
    if (data_socket < 0) {
        printf("DEBUG: Unable to accept data socket: [%i] %s\n", -data_socket, strerror(-data_socket));
        return data_socket;
    }
    printf("Connected to client!  Transferring data...\n");

    s32 result = callback(data_socket, arg);

    net_close(data_socket);
    if (result < 0) {
        printf("Error occurred while transferring data...\n");
    } else {
        printf("Finished transferring data!\n");
    }
    return result;
}

static s32 do_data_connection(client_t *client, void *callback, void *arg) {
    s32 result = write_reply(client, 150, "Transferring data.");
    if (result >= 0) {
        data_connection_handler handler = do_data_connection_active;
        if (client->passive_socket >= 0) handler = do_data_connection_passive;
        result = handler(client, (data_connection_callback)callback, arg);
        if (result < 0) {
            result = write_reply(client, 520, "Closing data connection, error occurred during transfer.");
        } else {
            result = write_reply(client, 226, "Closing data connection, transfer successful.");
        }
    }
    return result;
}

static s32 send_nlst(s32 data_socket, DIR_ITER *dir) {
    s32 result = 0;
    char filename[MAXPATHLEN + 2];
    struct stat st;
    while (dirnext(dir, filename, &st) == 0) {
        size_t end_index = strlen(filename);
        filename[end_index] = CRLF[0];
        filename[end_index + 1] = CRLF[1];
        filename[end_index + 2] = '\0';
        if ((result = write_exact(data_socket, filename, strlen(filename))) < 0) {
            break;
        }
    }
    return result;
}

static s32 send_list(s32 data_socket, DIR_ITER *dir) {
    s32 result = 0;
    char filename[MAXPATHLEN];
    struct stat st;
    char line[MAXPATHLEN + 56 + CRLF_LENGTH + 1];
    while (dirnext(dir, filename, &st) == 0) {
        sprintf(line, "%crwxr-xr-x    1 0        0     %11li Jan 01  1900 %s\r\n", (st.st_mode & S_IFDIR) ? 'd' : '-', st.st_size, filename); // what does it do > 2GB?
        if ((result = write_exact(data_socket, line, strlen(line))) < 0) {
            break;
        }
    }
    return result;
}

static s32 ftp_NLST(client_t *client, char *path) {
    if (!*path) {
        path = ".";
    }

    // TODO: VFS: handle case when path is vfs-root

    if (enter_cwd_context(client)) return write_reply(client, 550, "Could not enter cwd context");
    DIR_ITER *dir = with_virtual_path(diropen, path, 0, NULL);
    if (exit_cwd_context(client)) die("FATAL: Could not exit cwd context, exiting");

    if (dir == NULL) {
        return write_reply(client, 550, strerror(errno));
    }

    s32 result = do_data_connection(client, send_nlst, dir);

    dirclose(dir);
    return result;
}

static s32 ftp_LIST(client_t *client, char *path) {
    if (*path == '-') {
        // handle buggy clients that use "LIST -aL" or similar, at the expense of breaking paths that begin with '-'
        char flags[BUFFER_SIZE];
        char rest[BUFFER_SIZE];
        char *args[] = { flags, rest };
        split(path, ' ', 1, args);
        path = rest;
    }
    if (!*path) {
        path = ".";
    }

    // TODO: VFS: handle case when path is vfs-root

    if (enter_cwd_context(client)) return write_reply(client, 550, "Could not enter cwd context");
    DIR_ITER *dir = with_virtual_path(diropen, path, 0, NULL);
    if (exit_cwd_context(client)) die("FATAL: Could not exit cwd context, exiting");

    if (dir == NULL) {
        return write_reply(client, 550, strerror(errno));
    }

    s32 result = do_data_connection(client, send_list, dir);

    dirclose(dir);
    return result;
}

static s32 ftp_RETR(client_t *client, char *path) {
    if (enter_cwd_context(client)) return write_reply(client, 550, "Could not enter cwd context");
    FILE *f = with_virtual_path(fopen, path, 0, "rb", NULL);
    if (exit_cwd_context(client)) die("FATAL: Could not exit cwd context, exiting");

    if (!f) {
        return write_reply(client, 550, strerror(errno));
    }
    
    if (client->restart_marker && fseek(f, client->restart_marker, SEEK_SET)) {
        s32 fseek_error = errno;
        fclose(f);
        client->restart_marker = 0;
        return write_reply(client, 550, strerror(fseek_error));
    }
    client->restart_marker = 0;
    
    s32 result = do_data_connection(client, write_from_file, f);

    fclose(f);
    return result;
}

static s32 stor_or_append(client_t *client, FILE *f) {
    if (!f) {
        return write_reply(client, 550, strerror(errno));
    }

    s32 result = do_data_connection(client, read_to_file, f);

    fclose(f);
    return result;
}

static s32 ftp_STOR(client_t *client, char *path) {
    if (enter_cwd_context(client)) return write_reply(client, 550, "Could not enter cwd context");
    FILE *f = with_virtual_path(fopen, path, 0, "wb", NULL);
    if (exit_cwd_context(client)) die("FATAL: Could not exit cwd context, exiting");
    
    if (f && client->restart_marker && fseek(f, client->restart_marker, SEEK_SET)) {
        s32 fseek_error = errno;
        fclose(f);
        client->restart_marker = 0;
        return write_reply(client, 550, strerror(fseek_error));
    }
    client->restart_marker = 0;

    return stor_or_append(client, f);
}

static s32 ftp_APPE(client_t *client, char *path) {
    if (enter_cwd_context(client)) return write_reply(client, 550, "Could not enter cwd context");
    FILE *f = with_virtual_path(fopen, path, 0, "ab", NULL);
    if (exit_cwd_context(client)) die("FATAL: Could not exit cwd context, exiting");

    return stor_or_append(client, f);
}

static s32 ftp_REST(client_t *client, char *offset_str) {
    long offset;
    if (sscanf(offset_str, "%li", &offset) < 1 || offset < 0) {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    }
    client->restart_marker = offset;
    char msg[BUFFER_SIZE];
    sprintf(msg, "Restart position accepted (%li).", offset);
    return write_reply(client, 350, msg);
}

static s32 ftp_SITE(client_t *client, char *rest) {
    if (!strcasecmp("LOADER", rest)) {
        write_reply(client, 200, "Exiting to loader.");
        die("KTHXBYE");
    }
    return write_reply(client, 501, "Unknown SITE command.");
}

static s32 ftp_NOOP(client_t *client, char *rest) {
    return write_reply(client, 200, "NOOP command successful.");
}

static s32 ftp_SUPERFLUOUS(client_t *client, char *rest) {
    return write_reply(client, 202, "Command not implemented, superfluous at this site.");
}

static s32 ftp_UNKNOWN(client_t *client, char *rest) {
    return write_reply(client, 502, "Command not implemented.");
}

/*
    returns negative to signal an error that requires closing the connection
*/
static s32 process_command(client_t *client, char *cmd_line) {
    printf("Got command: %s\n", cmd_line);
    char cmd[BUFFER_SIZE], rest[BUFFER_SIZE];
    char *args[] = { cmd, rest };
    u32 num_args = split(cmd_line, ' ', 1, args); 
    if (num_args == 0) {
        return 0;
    }
    ftp_command_handler handler = ftp_UNKNOWN;
    if      (!strcasecmp("LIST", cmd)) handler = ftp_LIST;
    else if (!strcasecmp("PWD" , cmd)) handler = ftp_PWD;
    else if (!strcasecmp("CWD" , cmd)) handler = ftp_CWD;
    else if (!strcasecmp("CDUP", cmd)) handler = ftp_CDUP;
    else if (!strcasecmp("SIZE", cmd)) handler = ftp_SIZE;
    else if (!strcasecmp("PASV", cmd)) handler = ftp_PASV;
    else if (!strcasecmp("PORT", cmd)) handler = ftp_PORT;
    else if (!strcasecmp("TYPE", cmd)) handler = ftp_TYPE;
    else if (!strcasecmp("SYST", cmd)) handler = ftp_SYST;
    else if (!strcasecmp("MODE", cmd)) handler = ftp_MODE;
    else if (!strcasecmp("RETR", cmd)) handler = ftp_RETR;
    else if (!strcasecmp("STOR", cmd)) handler = ftp_STOR;
    else if (!strcasecmp("APPE", cmd)) handler = ftp_APPE;
    else if (!strcasecmp("REST", cmd)) handler = ftp_REST;
    else if (!strcasecmp("DELE", cmd)) handler = ftp_DELE;
    else if (!strcasecmp("MKD",  cmd)) handler = ftp_MKD;
    else if (!strcasecmp("RMD",  cmd)) handler = ftp_DELE;
    else if (!strcasecmp("RNFR", cmd)) handler = ftp_RNFR;
    else if (!strcasecmp("RNTO", cmd)) handler = ftp_RNTO;
    else if (!strcasecmp("NLST", cmd)) handler = ftp_NLST;
    else if (!strcasecmp("USER", cmd)) handler = ftp_USER;
    else if (!strcasecmp("PASS", cmd)) handler = ftp_PASS;
    else if (!strcasecmp("QUIT", cmd)) handler = ftp_QUIT;
    else if (!strcasecmp("REIN", cmd)) handler = ftp_REIN;
    else if (!strcasecmp("SITE", cmd)) handler = ftp_SITE;
    else if (!strcasecmp("ALLO", cmd)) handler = ftp_SUPERFLUOUS;
    else if (!strcasecmp("SITE", cmd)) handler = ftp_SUPERFLUOUS;
    else if (!strcasecmp("NOOP", cmd)) handler = ftp_NOOP;

    if     (handler == ftp_CWD ||
            handler == ftp_CDUP ||
            handler == ftp_DELE ||
            handler == ftp_RNTO ||
            handler == ftp_MKD ||
            handler == ftp_SIZE
    ) {
        return simple_cwd_context_handler(client, handler, rest);
    }

    return handler(client, rest);
}

static void *process_connection(void *client_ptr) {
    client_t *client = client_ptr;
    if (write_reply(client, 220, "ftpii") < 0) {
        printf("Error writing greeting.\n");
        goto recv_loop_end;
    }
    
    char buf[BUFFER_SIZE];
    s32 offset = 0;
    s32 bytes_read;
    while (offset < (BUFFER_SIZE - 1)) {
        char *offset_buf = buf + offset;
        if ((bytes_read = net_read(client->socket, offset_buf, BUFFER_SIZE - 1 - offset)) < 0) {
            printf("Read error %i occurred, closing client.\n", bytes_read);
            goto recv_loop_end;
        } else if (bytes_read == 0) {
            goto recv_loop_end; // EOF from client
        }
        offset += bytes_read;
        buf[offset] = '\0';
        
        if (strchr(offset_buf, '\0') != (buf + offset)) {
            printf("Received a null byte from client, closing connection ;-)\n"); // i have decided this isn't allowed =P
            goto recv_loop_end;
        }

        char *next;
        char *end;
        for (next = buf; (end = strstr(next, CRLF)); next = end + CRLF_LENGTH) {
            *end = '\0';
            if (strchr(next, '\n')) {
                printf("Received a line-feed from client without preceding carriage return, closing connection ;-)\n"); // i have decided this isn't allowed =P
                goto recv_loop_end;
            }
            
            if (*next) {
                s32 result;
                if ((result = process_command(client, next)) < 0) {
                    if (result != -EQUIT) {
                        printf("Closing connection due to error while processing command: %s\n", next);
                    }
                    goto recv_loop_end;
                }
            }
            
        }
        
        if (next != buf) { // some lines were processed
            offset = strlen(next);
            char tmp_buf[offset];
            memcpy(tmp_buf, next, offset);
            memcpy(buf, tmp_buf, offset);
        }
    }
    printf("Received line longer than %u bytes, closing client.\n", BUFFER_SIZE - 1);

    recv_loop_end:

    net_close(client->socket);
    close_passive_socket(client);
    free(client);

    printf("Done doing stuffs!\n");

    mutex_acquire(global_mutex);
    num_clients--;
    mutex_release(global_mutex);

    return NULL;
}

static void mainloop() {
    s32 server = create_server(PORT);
    printf("\nListening on TCP port %u...\n", PORT);
    while (1) {
        struct sockaddr_in client_address;
        s32 peer = accept_peer(server, &client_address);

        if (num_clients == MAX_CLIENTS) {
            printf("Maximum of %u clients reached, not accepting client.\n", MAX_CLIENTS);
            net_close(peer);
            continue;
        }

        client_t *client = malloc(sizeof(client_t));
        if (!client) {
            printf("Could not allocate memory for client state, not accepting client.\n");
            net_close(peer);
            continue;
        }
        client->socket = peer;
        client->representation_type = 'A';
        client->passive_socket = -1;
        strcpy(client->cwd, "fat3:/"); // TOOD: VFS: client should start in vfs-root
        *client->pending_rename = '\0';
        client->restart_marker = 0;
        memcpy(&client->address, &client_address, sizeof(client_address));

        lwp_t client_thread;
        if (LWP_CreateThread(&client_thread, process_connection, client, NULL, 0, 80)) {
            printf("Error creating client thread, not accepting client.\n");
            net_close(peer);
            free(client);
        } else {
            mutex_acquire(global_mutex);
            num_clients++;
            mutex_release(global_mutex);
        }
    }
}

int main(int argc, char **argv) {
    initialise_video();
    printf("\x1b[2;0H");
    initialise_fat();
    WPAD_Init();
    if (initialise_reset_button()) {
        printf("To exit, hold A on WiiMote #1 or press the reset button.\n");
    } else {
        printf("Unable to start reset thread - hold down the power button to exit.\n");
    }
    if (initialise_mount_buttons()) {
        printf("To remount internal SD, hold 1 on WiiMote #1.\n");
        printf("To remount USB storage, hold 2 on WiiMote #1.\n");
    } else {
        printf("Unable to start mount thread - no remounting while running!\n");
    }
    wait_for_network_initialisation();
    if (LWP_MutexInit(&global_mutex, true)) die("Could not initialise global mutex, exiting");
    if (LWP_MutexInit(&chdir_mutex, true)) die("Could not initialise chdir mutex, exiting");
    
    mainloop();

    return 0;
}
