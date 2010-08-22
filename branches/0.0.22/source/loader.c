// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#include <gctypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>

#include "dol.h"
#include "loader.h"

static bool read_from_file(u8 *buf, FILE *f) {
    while (1) {
        s32 bytes_read = fread(buf, 1, 0x8000, f);
        if (bytes_read > 0) buf += bytes_read;
        if (bytes_read < 0x8000) return feof(f);
    }
}

void load_from_file(FILE *f, char *arg) {
    struct __argv argv;
    bzero(&argv, sizeof(argv));
    argv.argvMagic = ARGV_MAGIC;
    argv.length = strlen(arg) + 2;
    argv.commandLine = malloc(argv.length);
    if (!argv.commandLine) return;
    strcpy(argv.commandLine, arg);
    argv.commandLine[argv.length - 1] = '\x00';
    argv.argc = 1;
    argv.argv = &argv.commandLine;
    argv.endARGV = argv.argv + 1;

    struct stat st;
    int fd = fileno(f);
    if (fstat(fd, &st)) return;
    u8 *buf = (u8 *)0x92000000;
    if (!read_from_file(buf, f)) return;

    run_dol(buf, &argv);

    free(argv.commandLine);
}
