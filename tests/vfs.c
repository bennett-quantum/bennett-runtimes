#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#ifndef VFS_PREFIX
#include "device_effects.c"
void __bennett_effect_console_write(const void *p,uint64_t n) { (void)p; (void)n; __builtin_trap(); }
#endif
#ifndef VFS_PREFIX
#define VFS_PREFIX ""
#endif
#define P(s) VFS_PREFIX s
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int main(void) {
    errno=0; CHECK(open(P("/missing"),O_RDONLY)==-1 && errno==ENOENT);
    CHECK(mkdir(P("/data"),0750)==0);
    int fd=open(P("/data/image"),O_CREAT|O_EXCL|O_RDWR,0640); CHECK(fd>=3);
    errno=0; CHECK(open(P("/data/image"),O_CREAT|O_EXCL|O_RDWR,0600)==-1 && errno==EEXIST);
    CHECK(write(fd,"abcdefgh",8)==8);
    CHECK(pwrite(fd,"ignored",0,100)==0);
    struct stat st; CHECK(fstat(fd,&st)==0 && st.st_size==8 && (st.st_mode&S_IFMT)==S_IFREG);
    int alias=dup(fd); CHECK(alias>=3 && alias!=fd);
    CHECK(lseek(alias,2,SEEK_SET)==2);
    char bytes[32]={0}; CHECK(read(fd,bytes,3)==3 && !memcmp(bytes,"cde",3));
    CHECK(close(alias)==0);
    CHECK(pwrite(fd,"XY",2,1)==2 && lseek(fd,0,SEEK_CUR)==5);
    CHECK(pread(fd,bytes,8,0)==8 && !memcmp(bytes,"aXYdefgh",8));
    CHECK(ftruncate(fd,16389)==0);
    CHECK(pread(fd,bytes,8,16381)==8);
    for(unsigned i=0;i<8;++i) CHECK(bytes[i]==0);
    CHECK(pwrite(fd,"tail",4,16385)==4);
    CHECK(pread(fd,bytes,4,16385)==4 && !memcmp(bytes,"tail",4));
    CHECK(ftruncate(fd,4)==0 && ftruncate(fd,16389)==0);
    CHECK(pread(fd,bytes,4,16385)==4);
    for(unsigned i=0;i<4;++i) CHECK(bytes[i]==0);
    CHECK(close(fd)==0);
    FILE *f=fopen(P("/data/image"),"r+"); CHECK(f);
    CHECK(fread(bytes,1,4,f)==4 && !memcmp(bytes,"aXYd",4));
    CHECK(fseek(f,0,SEEK_END)==0 && ftell(f)==16389);
    CHECK(fputs("!",f)>=0 && fclose(f)==0);
    f=fopen(P("/data/image"),"a"); CHECK(f && fseek(f,0,SEEK_SET)==0);
    CHECK(fwrite("end",1,3,f)==3 && fclose(f)==0);
    CHECK(stat(P("/data/image"),&st)==0 && st.st_size==16393);
    CHECK(rename(P("/data/image"),P("/data/image"))==0);
    errno=0; CHECK(open(P("/data/image/../wrong"),O_CREAT|O_RDWR,0600)==-1 && errno==ENOTDIR);
    CHECK(mkdir(P("/data/sub"),0700)==0);
    int directory=open(P("/data/sub"),O_RDONLY|O_DIRECTORY); CHECK(directory>=0);
    int relative=openat(directory,"../relative",O_CREAT|O_RDWR,0600); CHECK(relative>=0);
    CHECK(close(relative)==0 && unlink(P("/data/relative"))==0);
    CHECK(chdir(P("/data/sub"))==0);
    CHECK(rename(P("/data"),P("/moved"))==0);
    char here[1024]; CHECK(getcwd(here,sizeof here) && !strcmp(here,P("/moved/sub")));
    CHECK(chdir("/")==0 && close(directory)==0 && rmdir(P("/moved/sub"))==0);
    CHECK(rename(P("/moved"),P("/data"))==0);
    CHECK(rename(P("/data/image"),P("/data/renamed"))==0);
    errno=0; CHECK(stat(P("/data/image"),&st)==-1 && errno==ENOENT);
    fd=open(P("/data/renamed"),O_RDONLY); CHECK(fd>=0);
    CHECK(unlink(P("/data/renamed"))==0);
    CHECK(pread(fd,bytes,4,16389)==4 && !memcmp(bytes,"!end",4));
    CHECK(close(fd)==0 && rmdir(P("/data"))==0);
    /* A closed standard fd is reusable for an ordinary file. */
    CHECK(close(1)==0);
    fd=open(P("/reused"),O_CREAT|O_RDWR,0600); CHECK(fd==1);
    CHECK(write(fd,"R",1)==1 && pread(fd,bytes,1,0)==1 && bytes[0]=='R');
    CHECK(close(fd)==0 && unlink(P("/reused"))==0);
    return 0;
}
