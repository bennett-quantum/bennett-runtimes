#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int main(void) {
    errno=0; CHECK(open("/missing",O_RDONLY)==-1 && errno==ENOENT);
    CHECK(mkdir("/data",0750)==0);
    int fd=open("/data/image",O_CREAT|O_EXCL|O_RDWR,0640); CHECK(fd>=3);
    errno=0; CHECK(open("/data/image",O_CREAT|O_EXCL|O_RDWR,0600)==-1 && errno==EEXIST);
    CHECK(write(fd,"abcdefgh",8)==8);
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
    FILE *f=fopen("/data/image","r+"); CHECK(f);
    CHECK(fread(bytes,1,4,f)==4 && !memcmp(bytes,"aXYd",4));
    CHECK(fseek(f,0,SEEK_END)==0 && ftell(f)==16389);
    CHECK(fputs("!",f)>=0 && fclose(f)==0);
    f=fopen("/data/image","a"); CHECK(f && fseek(f,0,SEEK_SET)==0);
    CHECK(fwrite("end",1,3,f)==3 && fclose(f)==0);
    CHECK(stat("/data/image",&st)==0 && st.st_size==16393);
    CHECK(rename("/data/image","/data/renamed")==0);
    errno=0; CHECK(stat("/data/image",&st)==-1 && errno==ENOENT);
    fd=open("/data/renamed",O_RDONLY); CHECK(fd>=0);
    CHECK(unlink("/data/renamed")==0);
    CHECK(pread(fd,bytes,4,16389)==4 && !memcmp(bytes,"!end",4));
    CHECK(close(fd)==0 && rmdir("/data")==0);
    /* A closed standard fd is reusable for an ordinary file. */
    CHECK(close(1)==0);
    fd=open("/reused",O_CREAT|O_RDWR,0600); CHECK(fd==1);
    CHECK(write(fd,"R",1)==1 && pread(fd,bytes,1,0)==1 && bytes[0]=='R');
    CHECK(close(fd)==0 && unlink("/reused")==0);
    return 0;
}
