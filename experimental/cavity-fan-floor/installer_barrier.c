/* Validate the descriptor inherited from the shared installer which already
 * owns the directory barrier. No lock acquisition, API mutation or native I/O. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef NB_BARRIER
#define NB_BARRIER "/tmp/cc2-control-install.lock"
#endif
int main(int argc,char **argv){
    if(geteuid()!=0||argc!=3||strcmp(argv[1],"--check"))return 1;
    char *end=NULL;errno=0;long number=strtol(argv[2],&end,10);
    if(errno||!argv[2][0]||!end||*end||number<3||number>1048576)return 1;
    struct stat held,path;
    if(fstat((int)number,&held)||lstat(NB_BARRIER,&path))return 1;
    return !(S_ISDIR(held.st_mode)&&S_ISDIR(path.st_mode)&&held.st_uid==0&&path.st_uid==0&&
        (held.st_mode&07777)==0700&&(path.st_mode&07777)==0700&&
        held.st_dev==path.st_dev&&held.st_ino==path.st_ino);
}
