// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#ifndef _VRT_H_
#define _VRT_H_

#include <stdio.h>
#include <sys/dir.h>

char *to_real_path(char *virtual_cwd, char *virtual_path);

FILE *vrt_fopen(char *cwd, char *path, char *mode);
int vrt_stat(char *cwd, char *path, struct stat *st);
int vrt_chdir(char *cwd, char *path);
int vrt_unlink(char *cwd, char *path);
int vrt_mkdir(char *cwd, char *path, mode_t mode);
int vrt_rename(char *cwd, char *from_path, char *to_path);
DIR_ITER *vrt_diropen(char *cwd, char *path);
int vrt_dirnext(DIR_ITER *iter, char *filename, struct stat *st);
int vrt_dirclose(DIR_ITER *iter);

#endif /* _VRT_H_ */
