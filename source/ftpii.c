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
#include <wiiuse/wpad.h>

#include "common.h"
#include "ftp.h"

static const u16 PORT = 21;

static void initialise_ftpii() {
    initialise_video();
    initialise_global_mutex();
    initialise_fat();
    WPAD_Init();
    if (initialise_reset_button()) {
        printf("To exit, hold A on WiiMote #1 or press the reset button.\n");
    } else {
        printf("Unable to start reset thread - hold down the power button to exit.\n");
    }
    if (initialise_mount_buttons()) {
        printf("To remount device, hold 1 on WiiMote #1.\n");
    } else {
        printf("Unable to start mount thread - remounting on-the-fly will not work.\n");
    }
    wait_for_network_initialisation();
}

int main(int argc, char **argv) {
    initialise_ftpii();

    s32 server = create_server(PORT);
    printf("\nListening on TCP port %u...\n", PORT);
    while (1) {
        accept_ftp_client(server);
    }
}
