#include <sys/dir.h>
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
