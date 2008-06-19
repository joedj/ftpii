/*

Copyright (C) 2008 Daniel Ehlers <danielehlers@mindeye.net> 

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
include <sys/dir.h>
#include <sys/types.h>

typedef enum {RE_SD,RE_USB,RE_GC1,RE_GC2} vrt_entry_interface;

// basic virtual root functions
void vrt_Init();
int vrt_AddEntry(vrt_entry_interface id,char* name,char* alias);
void vrt_DelEntry(vrt_entry_interface id);
int vrt_SetMount(vrt_entry_interface id,int mounted);

// abstraction for base file/dir handling
FILE* vrt_fopen(char* path,char* mode);
int vrt_fclose(FILE *fp);
int vrt_stat(char* path,struct stat* status);
int vrt_chdir(char* path);
char* vrt_getcwd(char* buf,size_t size);
int vrt_unlink(char* path);
int vrt_mkdir(char* path, mode_t mode);
int vrt_rename(char* from_path,char* to_path);
DIR_ITER* vrt_diropen(char* path);
int vrt_dirnext(DIR_ITER *iter,char *filename, struct stat *filestat);
int vrt_dirclose(DIR_ITER *iter);
