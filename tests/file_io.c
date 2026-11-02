#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
extern void *__bennett_nz_alloc(unsigned long);
extern void __bennett_nz_free(void *);
int main(void) {
    char buf[32]="line one\nline two\n";
    char out[64];
    FILE *f=fmemopen(buf,18,"r");
    CHECK(f && fgets(out,sizeof out,f)==out && !strcmp(out,"line one\n"));
    CHECK(ftell(f)==9 && fgetc(f)=='l' && ungetc('l',f)=='l');
    CHECK(fread(out,1,9,f)==9 && !memcmp(out,"line two\n",9));
    CHECK(fgetc(f)==EOF && feof(f) && !ferror(f));
    rewind(f); CHECK(!feof(f) && ftell(f)==0);
    CHECK(fseek(f,-1,SEEK_SET)==-1 && errno==EINVAL);
    CHECK(fclose(f)==0);
    f=fmemopen(buf,sizeof buf,"w+"); CHECK(f);
    CHECK(fwrite("abc",1,3,f)==3 && fflush(f)==0 && !strcmp(buf,"abc"));
    CHECK(fseek(f,1,SEEK_SET)==0 && fputc('Z',f)=='Z' && fflush(f)==0);
    CHECK(!memcmp(buf,"aZc",3));
    fpos_t pos; CHECK(fgetpos(f,&pos)==0 && fseek(f,0,SEEK_SET)==0);
    CHECK(fread(out,1,3,f)==3 && !memcmp(out,"aZc",3));
    CHECK(fsetpos(f,&pos)==0 && ftell(f)==2 && fclose(f)==0);
    memcpy(buf,"abc",4); f=fmemopen(buf,sizeof buf,"a+"); CHECK(f);
    CHECK(fseek(f,0,SEEK_SET)==0 && fputc('D',f)=='D');
    CHECK(fclose(f)==0 && !strcmp(buf,"abcD"));
    /* Exact zero-size object: no accessible byte exists to initialise. */
    void *z=__bennett_nz_alloc(0);
    f=fmemopen(z,0,"w+"); CHECK(f && fwrite("x",1,1,f)==1);
    CHECK(fflush(f)==EOF && ferror(f)); clearerr(f); CHECK(!ferror(f));
    fclose(f); __bennett_nz_free(z);
    char small[4]={0}; f=fmemopen(small,sizeof small,"w+"); CHECK(f);
    CHECK(setvbuf(f,0,_IONBF,0)==0);
    errno=0; CHECK(fwrite("abcdef",1,6,f)==4 && ferror(f) && errno==ENOSPC);
    CHECK(!memcmp(small,"abcd",4)); fclose(f);
    char *dynamic=0; size_t n=0;
    f=open_memstream(&dynamic,&n); CHECK(f && dynamic && n==0);
    for (unsigned i=0;i<128;i++) CHECK(fputc((int)('a'+i%26),f)!=EOF);
    CHECK(fflush(f)==0 && n==128 && dynamic[n]==0);
    for (unsigned i=0;i<128;i++) CHECK(dynamic[i]==(char)('a'+i%26));
    CHECK(fseek(f,256,SEEK_SET)==0 && fputs("tail",f)>=0 && fclose(f)==0);
    CHECK(n==260 && !memcmp(dynamic+256,"tail",5));
    for (unsigned i=128;i<256;i++) CHECK(dynamic[i]==0);
    free(dynamic);
    return 0;
}
