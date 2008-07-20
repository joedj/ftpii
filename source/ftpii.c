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
#include <string.h>
#include <wiiuse/wpad.h>

#include "common.h"
#include "ftp.h"

static const u16 PORT = 21;
static const char *APP_DIR_PREFIX = "ftpii_";

static void initialise_ftpii() {
    initialise_video();
    initialise_global_mutex();
    WPAD_Init();
    if (initialise_reset_button()) {
        printf("To exit, hold A on WiiMote #1 or press the reset button.\n");
    } else {
        printf("Unable to start reset thread - hold down the power button to exit.\n");
    }
    wait_for_network_initialisation();
    initialise_fat();
    if (initialise_mount_buttons()) {
        printf("To remount a device, hold 1 on WiiMote #1.\n");
    } else {
        printf("Unable to start mount thread - remounting on-the-fly will not work.\n");
    }
}

static void set_password_from_executable(char *executable) {
    char *dir = basename(dirname(executable));
    if (strncasecmp(APP_DIR_PREFIX, dir, strlen(APP_DIR_PREFIX)) == 0) {
        set_ftp_password(dir + strlen(APP_DIR_PREFIX));
    }
}

int main(int argc, char **argv) {
    initialise_ftpii();

    if (argc > 1) {
        set_ftp_password(argv[1]);
    } else if (argc == 1) {
        set_password_from_executable(argv[0]);
    }

    s32 server = create_server(PORT);
    printf("\nListening on TCP port %u...\n", PORT);
    while (1) {
        accept_ftp_client(server);
    }
}
