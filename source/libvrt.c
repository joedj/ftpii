/*

ftpii -- an FTP server for the Wii

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


#include <ogcsys.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>


#include "libvrt.h"
struct virtual_root_entry {
  char* name;
  char* prefix;
  char* alias;
  int  mounted;
};

typedef struct virtual_root_entry vrt_entry;

#define MAX_VRT_ENTRYS 2
#define VRT_DEVICE_ID 38744
vrt_entry* rootfs[MAX_VRT_ENTRYS];

mutex_t fs_mutex;
char cwd[MAXPATHLEN];

/**
 * Init virtual root structs, must be called before using
 * this library.
 */ 
void vrt_Init(){
	// set default directory
	strcpy(cwd,"/");
	// create default mount points
	vrt_AddEntry(RE_SD,"sd","fat3:/");
	vrt_AddEntry(RE_USB,"usb","fat4:/");
}


/**
 * Add a new directory too root
 */
int vrt_AddEntry(vrt_entry_interface id,char* name,char* alias){
  if(rootfs[id] == NULL){
      char* prefix = (char*) malloc(sizeof(char)*strlen(name)+1);
      vrt_entry* entry;
      entry = (vrt_entry*) malloc(sizeof(vrt_entry));
      strcpy(prefix,"/");
      strcat(prefix,name);
      entry->prefix = prefix;
      entry->alias   = alias;
      entry->name    = name;
      entry->mounted = 0;
      rootfs[id] = entry;
      return 1;
    }
  return 0;
}

/**
 * Delete a directory from root
 */
void vrt_DelEntry(vrt_entry_interface id){
   vrt_entry* entry = rootfs[id];
   free(entry);
   rootfs[id] = NULL;
}

// Some stuff to be more thread safe
int lockMutex(){
  return LWP_MutexLock(fs_mutex);
}

int unlockMutex(){
  return LWP_MutexUnlock(fs_mutex);
}

int inRoot(){
  lockMutex();
  int in = (strlen(cwd) == 1 && cwd[0] == '/');
  unlockMutex();
  return in;
}

int isRoot(char* path){
  return (strlen(path) == 1 && cwd[0] == '/');
}

void setCWD(char* path){
  lockMutex();
  //printf("set CWD=%s\n",path);
  strcpy(cwd,path);
  unlockMutex();
}


/** 
 * Mark filesystem (directory) as mounted
 */
int vrt_SetMount(vrt_entry_interface id,int mounted){
  if(rootfs[id] != NULL){
    rootfs[id]->mounted = mounted;
    return 1;
  }
  return 0;
}

int vrt_isMounted(char* vrt_path){
  if(isRoot(vrt_path)){
    return 1;
  }
  int i;
  for(i = 0;i < MAX_VRT_ENTRYS; i++){
    if(rootfs[i] != NULL){
      if(strlen(rootfs[i]->prefix) <= strlen(vrt_path)){
        if(strncmp(vrt_path,rootfs[i]->prefix,strlen(rootfs[i]->prefix)) == 0){
	  return rootfs[i]->mounted;
	}
      }
    }
  }	
  return 0;
}

int vrt_isRootDir(char* vrt_path){
 if(isRoot(vrt_path)){
    return 1;
 }
 int i;
 for(i = 0;i < MAX_VRT_ENTRYS; i++){
    if(rootfs[i] != NULL){
      if(strlen(rootfs[i]->prefix) == strlen(vrt_path)){
        if(strncmp(vrt_path,rootfs[i]->prefix,strlen(rootfs[i]->prefix)) == 0){
	  return 1;
	}
      } 
    }
  }	
  return 0;
}

/**
 * Convert a absolut vrt path to an path libfat can handle
 */
int vrt_Path2Fat(char *vrt_path,char *fat_path){
  int i;
  for(i = 0;i < MAX_VRT_ENTRYS; i++){
    if(rootfs[i] != NULL){
      if(strlen(rootfs[i]->prefix) <= strlen(vrt_path)){
        if(strncmp(vrt_path,rootfs[i]->prefix,strlen(rootfs[i]->prefix)) == 0){
	  strcpy(fat_path,rootfs[i]->alias);
	  strcat(fat_path,vrt_path+strlen(rootfs[i]->prefix));
	  return 1;
	}
      }
    }
  }	
  return 0;
}

/**
 * Convert a libfat path to a absolut vrt path
 */ 
int vrt_Fat2Path(char *fat_path,char *vrt_path){
  int i;
  for(i = 0;i < MAX_VRT_ENTRYS; i++){
    if(rootfs[i] != NULL){
      if(strlen(rootfs[i]->alias) <= strlen(fat_path)){
        if(strncmp(fat_path,rootfs[i]->alias,strlen(rootfs[i]->alias)) == 0){
	  strcpy(vrt_path,rootfs[i]->prefix);
	  if(strlen(fat_path) > strlen(rootfs[i]->alias)){
		  strcat(vrt_path,"/");
        	  strcat(vrt_path,fat_path+strlen(rootfs[i]->alias));
	  }
	  return 1;
	}
      }
    }
  }	
  return 0;
}

/*
 * Abstract some of the default file and dir handling functions
 * so the upper layers could handle stuff with the vrt path.
 */

/**
 * fopen
 */
FILE* vrt_fopen(char* path,char* mode) {
  char fat_path[strlen(path)];
  if(vrt_Path2Fat(path,fat_path)){
	  return fopen(fat_path,mode);
  } else if(!inRoot()) {
      // try to open releative
      FILE* file = fopen(path,mode);
      if(file) return file;
  } 
  return NULL;
}

/**
 * fclose
 */ 
int vrt_fclose(FILE *fp){
	return fclose(fp);
}

/**
 * stat
 */ 
int vrt_stat(char* path,struct stat* status){
  char fat_path[strlen(path)];
  if(strlen(path) == 1 && path[0] == '/'){
    // TODO add some information to the stat struct 
    return 0;
  }
  if(vrt_Path2Fat(path,fat_path)){
    if(!vrt_isMounted(path)) {
	if(vrt_isRootDir(path)){
		return 0;
	} 
        return -1;
    } 
    return stat(fat_path,status);
  }
  //error(0,ENOENT,"");
  return -1;
}

/**
 * getcwd
 */
char* vrt_getcwd(char* buf,size_t size){
   char fat_path[size];
   /*getcwd(fat_path,size);
   if(strlen(fat_path) == 1 && fat_path[0] == '/'){
     strcpy(buf,"/");
     return buf;
   }
   if(vrt_Fat2Path(fat_path,buf)){
     return buf;
   } */
   strcpy(buf,cwd);
   return buf;
}

/**
 * chdir (only absolut path allowed)
 */
int vrt_chdir(char* path){
  char fat_path[strlen(path)];
  if(strlen(path) == 1 && path[0] == '/'){
     setCWD("/");
     return 0;
  }
  if(vrt_Path2Fat(path,fat_path)){
    if(!vrt_isMounted(path)) {
	    if(vrt_isRootDir(path)){
	      setCWD(path);
	      return 0;
	    }
	    return -1;
    }
    int result = chdir(fat_path);
    if(!result){
      setCWD(path);
    }
    return result;
  }
  //error(0,ENOENT,"");
  return -1;
}

/**
 * unlink
 */
int vrt_unlink(char* path){
  char fat_path[strlen(path)];
  if(vrt_Path2Fat(path,fat_path)){
   if(!vrt_isMounted(path)) return -1;
   return unlink(fat_path);
  } else if(!inRoot()) {
    int result = unlink(path);
    if(!result) return result;
  }
  //error(0,ENOENT,"");
  return -1;
}

/**
 * mkdir
 */
int vrt_mkdir(char* path, mode_t mode){
  char fat_path[strlen(path)];
  if(vrt_Path2Fat(path,fat_path)){
    if(!vrt_isMounted(path)) return -1;
    return mkdir(fat_path,mode);
  } else if(!inRoot()){
    if(vrt_isMounted(cwd)) return -1;
    return mkdir(path,mode);
  } 
  //error(0,ENOENT,"");
  return -1;
}

/**
 * rename 
 * TODO Also check that this ins't performed on root
 * TODO Check for relative stuff
 */
int vrt_rename(char* from_path,char* to_path){
  char fat_to_path[strlen(to_path)];
  char fat_from_path[strlen(from_path)];
  printf("vrt_rename(%s,%s)\n",from_path,to_path);
  if(vrt_Path2Fat(to_path,fat_to_path) && vrt_Path2Fat(from_path,fat_from_path)){
    printf("vrt_rename(%s,%s)\n",from_path,to_path);
    return rename(fat_from_path,fat_to_path);
  }
  //error(0,ENOENT,"");
  return -1;
}


/**
 * diropen
 * When in root this construct an DIR_ITER struct with default values.
 */
DIR_ITER* vrt_diropen(char* path){
  char fat_path[strlen(path)];
  if(strlen(path) == 1 && path[0] == '/'){
     DIR_ITER* iter;
     iter = (DIR_ITER*) malloc(sizeof(DIR_ITER));
     iter->device = VRT_DEVICE_ID;
     iter->dirStruct = 0;
     return iter;
  }
  if(!vrt_isMounted(path)){
    DIR_ITER* iter;
    iter = (DIR_ITER*) malloc(sizeof(DIR_ITER));
    iter->device = VRT_DEVICE_ID + 1;
    iter->dirStruct = 0;
    return iter;
  } else 
  if(vrt_Path2Fat(path,fat_path)){
    return diropen(fat_path);
  }
  //error(0,ENOENT,"");
  return NULL;
}


/**
 * dirnext 
 * When the DIRITER hast the VRT_DEVICE_ID this 
 * iter over our vrt_entrys
 */
int vrt_dirnext(DIR_ITER *iter,char *filename, struct stat *filestat){
  if(iter->device == VRT_DEVICE_ID){
   if((int) iter->dirStruct < MAX_VRT_ENTRYS && stat != NULL){
     filestat->st_mode = S_IFDIR;
     filestat->st_size = 31337;
     strcpy(filename,rootfs[(int) iter->dirStruct]->name);
     iter->dirStruct++;
     return 0;
   }
   return 1;
  } if(iter->device == VRT_DEVICE_ID + 1) {
    return 1;
  } else {
    return dirnext(iter, filename,filestat);
  } 
}

/**
 * dirreset
 */
int vrt_dirreset(DIR_ITER* iter){
  if(iter->device == VRT_DEVICE_ID){
    iter->dirStruct = 0;
    return 0;
  } else {
    return dirreset(iter);
  }
}

/**
 * dirclose
 */
int vrt_dirclose(DIR_ITER *iter){
  if(iter->device == VRT_DEVICE_ID || iter->device == VRT_DEVICE_ID + 1){
    free(iter);
    return 0;
  } else {
    return dirclose(iter);
  } 
}


