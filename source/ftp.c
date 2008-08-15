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
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include "common.h"
#include "vrt.h"

#define FTP_BUFFER_SIZE 512
#define MAX_CLIENTS 5

static const u16 SRC_PORT = 20;
static const s32 EQUIT = 696969;
static const char *CRLF = "\r\n";
static const u32 CRLF_LENGTH = 2;

static u8 num_clients = 0;
static u16 passive_port = 1024;
static char *password = NULL;

extern u32 net_gethostip();

typedef s32 (*data_connection_callback)(s32 data_socket, void *arg);

struct client_struct {
    s32 socket;
    char representation_type;
    s32 passive_socket;
    s32 data_socket;
    char cwd[MAXPATHLEN];
    char pending_rename[MAXPATHLEN];
    long restart_marker;
    struct sockaddr_in address;
    bool authenticated;
    char buf[FTP_BUFFER_SIZE];
    s32 offset;
    bool data_connection_connected;
    data_connection_callback data_callback;
    void *data_connection_callback_arg;
};

typedef struct client_struct client_t;

static client_t *clients[MAX_CLIENTS] = { NULL };

void set_ftp_password(char *new_password) {
    if (password) free((char *)password);
    if (new_password) {
        password = malloc(strlen(new_password) + 1);
        if (!password) die("Unable to allocate memory for password, exiting");
        strcpy((char *)password, new_password);
    } else {
        password = NULL;
    }
}

static bool compare_ftp_password(char *password_attempt) {
    return !password || !strcmp((char *)password, password_attempt);
}

/*
    TODO: support multi-line reply
*/
static s32 write_reply(client_t *client, u16 code, char *msg) {
    u32 msglen = 4 + strlen(msg) + CRLF_LENGTH;
    char msgbuf[msglen + 1];
    if (msgbuf == NULL) return -ENOMEM;
    sprintf(msgbuf, "%u %s\r\n", code, msg);
    printf("Wrote reply: %s", msgbuf);
    return send_exact(client->socket, msgbuf, msglen);
}

static void close_passive_socket(client_t *client) {
    if (client->passive_socket >= 0) {
        net_close_blocking(client->passive_socket);
        client->passive_socket = -1;
    }
}

static s32 ftp_USER(client_t *client, char *username) {
    return write_reply(client, 331, "User name okay, need password.");
}

static s32 ftp_PASS(client_t *client, char *password_attempt) {
    if (compare_ftp_password(password_attempt)) {
        client->authenticated = true;
        return write_reply(client, 230, "User logged in, proceed.");
    } else {
        return write_reply(client, 530, "Login incorrect.");
    }
}

static s32 ftp_REIN(client_t *client, char *rest) {
    close_passive_socket(client);
    strcpy(client->cwd, "/");
    client->representation_type = 'A';
    client->authenticated = false;
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
    char representation_type[FTP_BUFFER_SIZE], param[FTP_BUFFER_SIZE];
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
    sprintf(msg, "\"%s\" is current directory.", client->cwd);
    return write_reply(client, 257, msg);
}

static s32 ftp_CWD(client_t *client, char *path) {
    s32 result;
    if (!vrt_chdir(client->cwd, path)) {
        result = write_reply(client, 250, "CWD command successful.");
    } else  {
        result = write_reply(client, 550, strerror(errno));
    }
    return result;
}

static s32 ftp_CDUP(client_t *client, char *rest) {
    s32 result;
    if (!vrt_chdir(client->cwd, "..")) {
        result = write_reply(client, 250, "CDUP command successful.");
    } else  {
        result = write_reply(client, 550, strerror(errno));
    }
    return result;
}

static s32 ftp_DELE(client_t *client, char *path) {
    if (!vrt_unlink(client->cwd, path)) {
        return write_reply(client, 250, "File or directory removed.");
    } else {
        return write_reply(client, 550, strerror(errno));
    }
}

static s32 ftp_MKD(client_t *client, char *path) {
    if (!*path) {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    }
    if (!vrt_mkdir(client->cwd, path, 0777)) {
        char msg[MAXPATHLEN + 21];
        char abspath[MAXPATHLEN];
        strcpy(abspath, client->cwd);
        vrt_chdir(abspath, path); // TODO: error checking
        // TODO: escape double-quotes
        sprintf(msg, "\"%s\" directory created.", abspath);
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
    if (!vrt_rename(client->cwd, client->pending_rename, path)) {
        result = write_reply(client, 250, "Rename successful.");
    } else {
        result = write_reply(client, 550, strerror(errno));
    }
    *client->pending_rename = '\0';
    return result;
}

static s32 ftp_SIZE(client_t *client, char *path) {
    struct stat st;
    if (!vrt_stat(client->cwd, path, &st)) {
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
    set_blocking(client->passive_socket, false);
    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(passive_port++); // XXX: BUG: This will overflow eventually, with interesting results...
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

typedef s32 (*data_connection_handler)(client_t *client, data_connection_callback callback, void *arg);

static s32 prepare_data_connection_active(client_t *client, data_connection_callback callback, void *arg) {
    s32 data_socket = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (data_socket < 0) {
        printf("DEBUG: Unable to create data socket: [%i] %s\n", -data_socket, strerror(-data_socket));
        return data_socket;
    }
    set_blocking(data_socket, false);
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
    
    client->data_socket = data_socket;
    client->data_connection_connected = false;
    client->data_callback = callback;
    client->data_connection_callback_arg = arg;
    printf("Attempting to connect to client at %s:%u\n", inet_ntoa(client->address.sin_addr), ntohs(client->address.sin_port));
    return 0;
}

static s32 prepare_data_connection_passive(client_t *client, data_connection_callback callback, void *arg) {
    client->data_socket = client->passive_socket;
    client->data_connection_connected = false;
    client->data_callback = callback;
    client->data_connection_callback_arg = arg;
    printf("Waiting for data connections...\n");
    return 0;
}

static s32 prepare_data_connection(client_t *client, void *callback, void *arg) {
    s32 result = write_reply(client, 150, "Transferring data.");
    if (result >= 0) {
        data_connection_handler handler = prepare_data_connection_active;
        if (client->passive_socket >= 0) handler = prepare_data_connection_passive;
        result = handler(client, (data_connection_callback)callback, arg);
        if (result < 0) {
            result = write_reply(client, 520, "Closing data connection, error occurred during transfer.");
        }
    }
    return result;
}

static s32 send_nlst(s32 data_socket, DIR_ITER *dir) {
    s32 result = 0;
    char filename[MAXPATHLEN + 2];
    struct stat st;
    while (vrt_dirnext(dir, filename, &st) == 0) {
        size_t end_index = strlen(filename);
        filename[end_index] = CRLF[0];
        filename[end_index + 1] = CRLF[1];
        filename[end_index + 2] = '\0';
        if ((result = send_exact(data_socket, filename, strlen(filename))) < 0) {
            break;
        }
    }
    vrt_dirclose(dir);
    return result < 0 ? result : 0;
}

static s32 send_list(s32 data_socket, DIR_ITER *dir) {
    s32 result = 0;
    char filename[MAXPATHLEN];
    struct stat st;
    char line[MAXPATHLEN + 56 + CRLF_LENGTH + 1];
    while (vrt_dirnext(dir, filename, &st) == 0) {
        sprintf(line, "%crwxr-xr-x    1 0        0     %11li Jan 01  1970 %s\r\n", (st.st_mode & S_IFDIR) ? 'd' : '-', st.st_size, filename); // what does it do > 2GB?
        if ((result = send_exact(data_socket, line, strlen(line))) < 0) {
            break;
        }
    }
    vrt_dirclose(dir);
    return result < 0 ? result : 0;
}

static s32 ftp_NLST(client_t *client, char *path) {
    if (!*path) {
        path = ".";
    }

    DIR_ITER *dir = vrt_diropen(client->cwd, path);
    if (dir == NULL) {
        return write_reply(client, 550, strerror(errno));
    }

    s32 result = prepare_data_connection(client, send_nlst, dir);
    if (result < 0) vrt_dirclose(dir);
    return result;
}

static s32 ftp_LIST(client_t *client, char *path) {
    if (*path == '-') {
        // handle buggy clients that use "LIST -aL" or similar, at the expense of breaking paths that begin with '-'
        char flags[FTP_BUFFER_SIZE];
        char rest[FTP_BUFFER_SIZE];
        char *args[] = { flags, rest };
        split(path, ' ', 1, args);
        path = rest;
    }
    if (!*path) {
        path = ".";
    }

    DIR_ITER *dir = vrt_diropen(client->cwd, path);
    if (dir == NULL) {
        return write_reply(client, 550, strerror(errno));
    }

    s32 result = prepare_data_connection(client, send_list, dir);
    if (result < 0) vrt_dirclose(dir);
    return result;
}

static s32 ftp_RETR(client_t *client, char *path) {
    FILE *f = vrt_fopen(client->cwd, path, "rb");
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
    
    s32 result = prepare_data_connection(client, send_from_file, f);
    if (result < 0) fclose(f);
    return result;
}

static s32 stor_or_append(client_t *client, FILE *f) {
    if (!f) {
        return write_reply(client, 550, strerror(errno));
    }
    s32 result = prepare_data_connection(client, recv_to_file, f);
    if (result < 0) fclose(f);
    return result;
}

static s32 ftp_STOR(client_t *client, char *path) {
    FILE *f = vrt_fopen(client->cwd, path, "wb");
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
    return stor_or_append(client, vrt_fopen(client->cwd, path, "ab"));
}

static s32 ftp_REST(client_t *client, char *offset_str) {
    long offset;
    if (sscanf(offset_str, "%li", &offset) < 1 || offset < 0) {
        return write_reply(client, 501, "Syntax error in parameters or arguments.");
    }
    client->restart_marker = offset;
    char msg[FTP_BUFFER_SIZE];
    sprintf(msg, "Restart position accepted (%li).", offset);
    return write_reply(client, 350, msg);
}

static s32 ftp_SITE_LOADER(client_t *client, char *rest) {
    s32 result = write_reply(client, 200, "Exiting to loader.");
    set_reset_flag();
    return result;
}

static s32 ftp_SITE_CLEAR(client_t *client, char *rest) {
    s32 result = write_reply(client, 200, "Cleared.");
    u32 i;
    for (i = 0; i < 100; i++) printf("\n");
    printf("\x1b[2;0H");
    return result;
}

/*
    This is implemented as a no-op to prevent some FTP clients
    from displaying skip/abort/retry type prompts.
*/
static s32 ftp_SITE_CHMOD(client_t *client, char *rest) {
    return write_reply(client, 250, "SITE CHMOD command ok.");
}

static s32 ftp_SITE_PASSWD(client_t *client, char *new_password) {
    set_ftp_password(new_password);
    return write_reply(client, 200, "Password changed.");
}

static s32 ftp_SITE_NOPASSWD(client_t *client, char *rest) {
    set_ftp_password(NULL);
    return write_reply(client, 200, "Authentication disabled.");
}

static s32 ftp_SITE_UNKNOWN(client_t *client, char *rest) {
    return write_reply(client, 501, "Unknown SITE command.");
}

typedef s32 (*ftp_command_handler)(client_t *client, char *args);

static s32 dispatch_to_handler(client_t *client, char *cmd_line, const char **commands, const ftp_command_handler *handlers) {
    char cmd[FTP_BUFFER_SIZE], rest[FTP_BUFFER_SIZE];
    char *args[] = { cmd, rest };
    split(cmd_line, ' ', 1, args); 
    s32 i;
    for (i = 0; commands[i]; i++) {
        if (!strcasecmp(commands[i], cmd)) break;
    }
    return handlers[i](client, rest);
}

static const char *site_commands[] = { "LOADER", "CLEAR", "CHMOD", "PASSWD", "NOPASSWD", NULL };
static const ftp_command_handler site_handlers[] = { ftp_SITE_LOADER, ftp_SITE_CLEAR, ftp_SITE_CHMOD, ftp_SITE_PASSWD, ftp_SITE_NOPASSWD, ftp_SITE_UNKNOWN };

static s32 ftp_SITE(client_t *client, char *cmd_line) {
    return dispatch_to_handler(client, cmd_line, site_commands, site_handlers);
}

static s32 ftp_NOOP(client_t *client, char *rest) {
    return write_reply(client, 200, "NOOP command successful.");
}

static s32 ftp_SUPERFLUOUS(client_t *client, char *rest) {
    return write_reply(client, 202, "Command not implemented, superfluous at this site.");
}

static s32 ftp_NEEDAUTH(client_t *client, char *rest) {
    return write_reply(client, 530, "Please login with USER and PASS.");
}

static s32 ftp_UNKNOWN(client_t *client, char *rest) {
    return write_reply(client, 502, "Command not implemented.");
}

static const char *unauthenticated_commands[] = { "USER", "PASS", "QUIT", "REIN", "NOOP", NULL };
static const ftp_command_handler unauthenticated_handlers[] = { ftp_USER, ftp_PASS, ftp_QUIT, ftp_REIN, ftp_NOOP, ftp_NEEDAUTH };

static const char *authenticated_commands[] = {
    "USER", "PASS", "LIST", "PWD", "CWD", "CDUP",
    "SIZE", "PASV", "PORT", "TYPE", "SYST", "MODE",
    "RETR", "STOR", "APPE", "REST", "DELE", "MKD",
    "RMD", "RNFR", "RNTO", "NLST", "QUIT", "REIN",
    "SITE", "NOOP", "ALLO", NULL
};
static const ftp_command_handler authenticated_handlers[] = {
    ftp_USER, ftp_PASS, ftp_LIST, ftp_PWD, ftp_CWD, ftp_CDUP,
    ftp_SIZE, ftp_PASV, ftp_PORT, ftp_TYPE, ftp_SYST, ftp_MODE,
    ftp_RETR, ftp_STOR, ftp_APPE, ftp_REST, ftp_DELE, ftp_MKD,
    ftp_DELE, ftp_RNFR, ftp_RNTO, ftp_NLST, ftp_QUIT, ftp_REIN,
    ftp_SITE, ftp_NOOP, ftp_SUPERFLUOUS, ftp_UNKNOWN
};

/*
    returns negative to signal an error that requires closing the connection
*/
static s32 process_command(client_t *client, char *cmd_line) {
    if (strlen(cmd_line) == 0) {
        return 0;
    }

    printf("Got command: %s\n", cmd_line);

    const char **commands = unauthenticated_commands;
    const ftp_command_handler *handlers = unauthenticated_handlers;

    if (client->authenticated) {
        commands = authenticated_commands;
        handlers = authenticated_handlers;
    }

    return dispatch_to_handler(client, cmd_line, commands, handlers);
}

static void cleanup_client(client_t *client) {
    net_close_blocking(client->socket);
    if (client->data_socket >= 0 && client->data_socket != client->passive_socket) {
        net_close_blocking(client->data_socket);
    }
    client->data_socket = -1;
    client->data_connection_connected = false;
    client->data_callback = NULL;
    client->data_connection_callback_arg = NULL;
    close_passive_socket(client);
    int client_index;
    for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
        if (clients[client_index] == client) {
            clients[client_index] = NULL;
            break;
        }
    }
    free(client);
    num_clients--;
    printf("Client disconnected.\n");
}

static void process_accept_events(s32 server) {
    s32 peer;
    struct sockaddr_in client_address;
    socklen_t addrlen = sizeof(client_address);
    while ((peer = net_accept(server, (struct sockaddr *)&client_address, &addrlen)) != -EAGAIN) {
        if (peer < 0) {
            net_close(server);
            die("Error accepting connection");
        }

        printf("Accepted connection from %s!\n", inet_ntoa(client_address.sin_addr));

        if (num_clients == MAX_CLIENTS) {
            printf("Maximum of %u clients reached, not accepting client.\n", MAX_CLIENTS);
            net_close(peer);
            return;
        }

        client_t *client = malloc(sizeof(client_t));
        if (!client) {
            printf("Could not allocate memory for client state, not accepting client.\n");
            net_close(peer);
            return;
        }
        client->socket = peer;
        client->representation_type = 'A';
        client->passive_socket = -1;
        client->data_socket = -1;
        strcpy(client->cwd, "/");
        *client->pending_rename = '\0';
        client->restart_marker = 0;
        client->authenticated = false;
        client->offset = 0;
        client->data_connection_connected = false;
        client->data_callback = NULL;
        client->data_connection_callback_arg = NULL;
        memcpy(&client->address, &client_address, sizeof(client_address));
        int client_index;
        if (write_reply(client, 220, "ftpii") < 0) {
            printf("Error writing greeting.\n");
            net_close_blocking(peer);
            free(client);
        } else {
            for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
                if (!clients[client_index]) {
                    clients[client_index] = client;
                    break;
                }
            }
            num_clients++;
        }
    }
}

static void process_data_events(client_t *client) {
    s32 result;
    if (!client->data_connection_connected) {
        if (client->passive_socket >= 0) {
            struct sockaddr_in data_peer_address;
            socklen_t addrlen = sizeof(data_peer_address);
            result = net_accept(client->passive_socket, (struct sockaddr *)&data_peer_address ,&addrlen);
            if (result < 0 && result != -EAGAIN) {
                printf("DEBUG: Unable to accept data socket: [%i] %s\n", -result, strerror(-result));
            } else if (result >= 0) {
                client->data_socket = result;
                client->data_connection_connected = true;
            }
        } else {
            if ((result = net_connect(client->data_socket, (struct sockaddr *)&client->address, sizeof(client->address))) < 0) {
                if (result == -EINPROGRESS) result = -EAGAIN;
                if (result != -EAGAIN && result != -EISCONN) printf("Unable to connect to client: [%i] %s\n", -result, strerror(-result));
            }
             if (result >= 0 || result == -EISCONN) {
                client->data_connection_connected = true;
            }
        }
        if (client->data_connection_connected) {
            result = 1;
            printf("Connected to client!  Transferring data...\n");
        }
    } else {
        result = client->data_callback(client->data_socket, client->data_connection_callback_arg);
    }

    if (result <= 0 && result != -EAGAIN) {
        if (client->data_socket >= 0 && client->data_socket != client->passive_socket) {
            net_close_blocking(client->data_socket);
        }
        client->data_socket = -1;
        client->data_connection_connected = false;
        client->data_callback = NULL;
        client->data_connection_callback_arg = NULL;
        if (result < 0) {
            result = write_reply(client, 520, "Closing data connection, error occurred during transfer.");
        } else {
            result = write_reply(client, 226, "Closing data connection, transfer successful.");
        }
        if (result < 0) {
            cleanup_client(client);
        }
    }
}

static void process_control_events(client_t *client) {
    s32 bytes_read;
    while (client->offset < (FTP_BUFFER_SIZE - 1)) {
        if (client->data_callback) {
            return;
        }
        char *offset_buf = client->buf + client->offset;
        if ((bytes_read = net_read(client->socket, offset_buf, FTP_BUFFER_SIZE - 1 - client->offset)) < 0) {
            if (bytes_read != -EAGAIN) {
                printf("Read error %i occurred, closing client.\n", bytes_read);
                goto recv_loop_end;
            }
            return;
        } else if (bytes_read == 0) {
            goto recv_loop_end; // EOF from client
        }
        client->offset += bytes_read;
        client->buf[client->offset] = '\0';
    
        if (strchr(offset_buf, '\0') != (client->buf + client->offset)) {
            printf("Received a null byte from client, closing connection ;-)\n"); // i have decided this isn't allowed =P
            goto recv_loop_end;
        }

        char *next;
        char *end;
        for (next = client->buf; (end = strstr(next, CRLF)) && !client->data_callback; next = end + CRLF_LENGTH) {
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
    
        if (next != client->buf) { // some lines were processed
            client->offset = strlen(next);
            char tmp_buf[client->offset];
            memcpy(tmp_buf, next, client->offset);
            memcpy(client->buf, tmp_buf, client->offset);
        }
    }
    printf("Received line longer than %u bytes, closing client.\n", FTP_BUFFER_SIZE - 1);

    recv_loop_end:
    cleanup_client(client);
}

void process_ftp_events(s32 server) {
    process_accept_events(server);
    int client_index;
    for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
        client_t *client = clients[client_index];
        if (client) {
            if (client->data_callback) {
                process_data_events(client);
            } else {
                process_control_events(client);
            }
        }
    }
    usleep(5000);
}
