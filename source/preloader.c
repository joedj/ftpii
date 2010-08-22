// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <gccore.h>

#include "_ftpii_dol.h"
#include "dol.h"

int main(int argc, char **argv) {
    VIDEO_Init();
    run_dol(_ftpii_dol, __system_argv);
    return 1;
}
