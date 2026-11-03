/* A data-image filesystem: namespace, inodes, sparse pages and descriptors
 * are ordinary program data. No host file, hidden cache or fixed file cap. */
#define _GNU_SOURCE
#include "vfs.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct page { uint64_t number; struct page *next; unsigned char bytes[4096]; };
struct bennett_inode {
    enum bennett_file_kind kind;
    unsigned mode;
    uint64_t number, size, links;
    struct page *pages;
};
struct name { char *path; struct bennett_inode *inode; struct name *next; };
struct descriptor { long fd; int cloexec; struct bennett_file *file; struct descriptor *next; };
static struct bennett_inode initial_inodes[] = {
    {BRT_DIRECTORY,S_IFDIR|0755,1,0,1,0}, {BRT_DIRECTORY,S_IFDIR|0755,2,0,1,0},
    {BRT_ENTROPY,S_IFCHR|0666,3,0,1,0}, {BRT_ENTROPY,S_IFCHR|0666,4,0,1,0},
    {BRT_NULL,S_IFCHR|0666,5,0,1,0}, {BRT_ZERO,S_IFCHR|0666,6,0,1,0},
    {BRT_BLOCK,S_IFBLK|0660,7,0,1,0}, {BRT_BLOCK,S_IFBLK|0660,8,0,1,0},
    {BRT_CONSOLE,S_IFCHR|0666,9,0,1,0}
};
static struct name initial_names[9] = {
    {"/",initial_inodes,&initial_names[1]}, {"/dev",initial_inodes+1,&initial_names[2]},
    {"/dev/random",initial_inodes+2,&initial_names[3]}, {"/dev/urandom",initial_inodes+3,&initial_names[4]},
    {"/dev/null",initial_inodes+4,&initial_names[5]}, {"/dev/zero",initial_inodes+5,&initial_names[6]},
    {"/dev/block",initial_inodes+6,&initial_names[7]}, {"/dev/vda",initial_inodes+7,&initial_names[8]},
    {"/dev/console",initial_inodes+8,0}
};
static struct name *names=initial_names;
static uint64_t next_inode=10;
static char *cwd="/";
static struct bennett_file standard_files[3] = {
    {BRT_INPUT,O_RDONLY,0,1,0}, {BRT_CONSOLE,O_WRONLY,0,1,0}, {BRT_CONSOLE,O_WRONLY,0,1,0}
};
static struct descriptor standard_fds[3] = {
    {0,0,standard_files,&standard_fds[1]}, {1,0,standard_files+1,&standard_fds[2]},
    {2,0,standard_files+2,0}
};
static struct descriptor *fds=standard_fds;

void __bennett_rt_vfs_prepare(void) {
    for (unsigned i=0;i<3;++i) {
        standard_files[i]=(struct bennett_file){i?BRT_CONSOLE:BRT_INPUT,i?O_WRONLY:O_RDONLY,0,1,0};
        standard_fds[i]=(struct descriptor){i,0,standard_files+i,i<2?standard_fds+i+1:0};
    }
    fds=standard_fds;
}
static struct descriptor *descriptor(long fd) {
    for(struct descriptor *d=fds;d;d=d->next) if(d->fd==fd) return d;
    return 0;
}
struct bennett_file *__bennett_rt_vfs_file(long fd) {
    struct descriptor *d=descriptor(fd); return d?d->file:0;
}
static long install(struct bennett_file *f,long start,int exact,int cloexec) {
    if(start<0 || start>INT_MAX) return -EINVAL;
    long id=start;
    if(!exact) while(__bennett_rt_vfs_file(id)) { if(id==INT_MAX) return -EMFILE; ++id; }
    struct descriptor *d=descriptor(id);
    if(!d) { d=malloc(sizeof *d); *d=(struct descriptor){id,0,0,fds}; fds=d; }
    if(d->file) --d->file->refs;
    d->file=f; d->cloexec=cloexec; ++f->refs;
    return id;
}
long __bennett_rt_vfs_close(long fd) {
    struct descriptor *d=descriptor(fd);
    if(!d || !d->file) return -EBADF;
    --d->file->refs; d->file=0; return 0;
}
long __bennett_rt_vfs_dup(long old,long start,int exact,int cloexec) {
    struct bennett_file *f=__bennett_rt_vfs_file(old);
    if(!f) return -EBADF;
    if(exact && (start<0 || start>INT_MAX)) return -EBADF;
    if(exact && old==start) return old;
    return install(f,start,exact,cloexec);
}
static struct name *lookup(const char *path) {
    for(struct name *n=names;n;n=n->next) if(n->inode && !strcmp(path,n->path)) return n;
    return 0;
}
/* Canonical paths have no host NAME_MAX/PATH_MAX or stack-buffer bound. */
static char *canonical(const char *path,long at,int *error) {
    if(!path) { *error=EFAULT; return 0; }
    if(!*path) { *error=ENOENT; return 0; }
    const char *base=cwd;
    if(*path!='/' && at!=AT_FDCWD) {
        struct bennett_file *f=__bennett_rt_vfs_file(at);
        if(!f) { *error=EBADF; return 0; }
        if(f->kind!=BRT_DIRECTORY) { *error=ENOTDIR; return 0; }
        base=0;
        for(struct name *n=names;n;n=n->next) if(n->inode==f->inode) { base=n->path; break; }
        if(!base) { *error=ENOENT; return 0; }
    }
    size_t a=*path=='/'?0:strlen(base), b=strlen(path);
    if(b>SIZE_MAX-a-2) { *error=ENAMETOOLONG; return 0; }
    char *result=malloc(a+b+2); size_t used=1; result[0]='/';
    if(a>1) { memcpy(result,base,a); used=a; }
    while(*path) {
        while(*path=='/') ++path;
        const char *begin=path; while(*path && *path!='/') ++path;
        size_t len=path-begin;
        if(!len || (len==1 && begin[0]=='.')) continue;
        if(len==2 && begin[0]=='.' && begin[1]=='.') {
            while(used>1 && result[used-1]!='/') --used;
            if(used>1) --used;
        } else {
            if(used>1) result[used++]='/';
            memcpy(result+used,begin,len); used+=len;
        }
        const char *rest=path; while(*rest=='/') ++rest;
        if(*rest) {
            result[used]=0; struct name *n=lookup(result);
            if(!n || n->inode->kind!=BRT_DIRECTORY) {
                *error=n?ENOTDIR:ENOENT; free(result); return 0;
            }
        }
    }
    result[used]=0; return result;
}
static int parent_directory(char *path) {
    char *slash=strrchr(path,'/'); if(!slash) return -ENOENT;
    char old=slash[1]; if(slash==path) slash[1]=0; else *slash=0;
    struct name *p=lookup(path); int r=!p?-ENOENT:p->inode->kind!=BRT_DIRECTORY?-ENOTDIR:0;
    if(slash==path) slash[1]=old; else *slash='/'; return r;
}
static struct name *create_name(char *path,unsigned mode,enum bennett_file_kind kind) {
    struct bennett_inode *i=malloc(sizeof *i); *i=(struct bennett_inode){kind,mode,next_inode++,0,1,0};
    struct name *n=malloc(sizeof *n); *n=(struct name){path,i,names}; names=n; return n;
}
static void truncate_inode(struct bennett_inode *i,uint64_t size) {
    if(size<i->size) {
        struct page **link=&i->pages;
        while(*link) {
            struct page *p=*link;
            if(p->number>size/4096 || (p->number==size/4096 && !(size%4096))) {
                *link=p->next; free(p);
            } else {
                if(p->number==size/4096) memset(p->bytes+size%4096,0,4096-size%4096);
                link=&p->next;
            }
        }
    }
    i->size=size;
}
long __bennett_rt_vfs_open(const char *path,int flags,unsigned mode,long at) {
    if((flags&O_ACCMODE)==O_ACCMODE) return -EINVAL;
    int error=0; char *full=canonical(path,at,&error); if(!full) return -error;
    struct name *n=lookup(full);
    if((flags&O_TMPFILE)==O_TMPFILE) {
        if(!n) { free(full); return -ENOENT; }
        if(n->inode->kind!=BRT_DIRECTORY) { free(full); return -ENOTDIR; }
        if((flags&O_ACCMODE)==O_RDONLY) { free(full); return -EINVAL; }
        free(full);
        struct bennett_inode *i=calloc(1,sizeof *i);
        i->kind=BRT_REGULAR; i->mode=S_IFREG|(mode&07777); i->number=next_inode++;
        struct bennett_file *f=malloc(sizeof *f);
        *f=(struct bennett_file){BRT_REGULAR,flags,0,0,i}; return install(f,0,0,!!(flags&O_CLOEXEC));
    }
    if(!n) {
        if(!(flags&O_CREAT)) { free(full); return -ENOENT; }
        int r=parent_directory(full); if(r) { free(full); return r; }
        n=create_name(full,S_IFREG|(mode&07777),BRT_REGULAR); full=0;
    } else if((flags&(O_CREAT|O_EXCL))==(O_CREAT|O_EXCL)) { free(full); return -EEXIST; }
    free(full);
    if((flags&O_DIRECTORY) && n->inode->kind!=BRT_DIRECTORY) return -ENOTDIR;
    if(n->inode->kind==BRT_DIRECTORY && ((flags&O_ACCMODE)!=O_RDONLY || (flags&O_CREAT))) return -EISDIR;
    if((flags&O_TRUNC) && n->inode->kind==BRT_REGULAR && (flags&O_ACCMODE)!=O_RDONLY)
        truncate_inode(n->inode,0);
    struct bennett_file *f=malloc(sizeof *f);
    *f=(struct bennett_file){n->inode->kind,flags,0,0,n->inode};
    return install(f,0,0,!!(flags&O_CLOEXEC));
}
long __bennett_rt_vfs_io(struct bennett_file *f,void *buffer,uint64_t n,int writing,uint64_t offset,int positional) {
    if((f->flags&O_PATH) || (writing?(f->flags&O_ACCMODE)==O_RDONLY:(f->flags&O_ACCMODE)==O_WRONLY)) return -EBADF;
    if(f->kind==BRT_DIRECTORY) return -EISDIR;
    if(f->kind==BRT_NULL) return writing?(long)n:0;
    if(f->kind==BRT_ZERO) { if(!writing) memset(buffer,0,n); return n; }
    if(!n) return 0;
    struct bennett_inode *i=f->inode;
    uint64_t pos=positional?offset:f->pos;
    if(writing && (f->flags&O_APPEND)) pos=i->size;
    if(pos>LONG_MAX || n>(uint64_t)LONG_MAX-pos) return -EFBIG;
    if(!writing) { if(pos>=i->size) n=0; else if(n>i->size-pos) n=i->size-pos; }
    unsigned char *bytes=buffer; uint64_t done=0;
    while(done<n) {
        uint64_t number=(pos+done)/4096, index=(pos+done)%4096, count=4096-index;
        if(count>n-done) count=n-done;
        struct page *p=i->pages;
        while(p && p->number!=number) p=p->next;
        if(writing && !p) { p=calloc(1,sizeof *p); p->number=number; p->next=i->pages; i->pages=p; }
        if(writing) memcpy(p->bytes+index,bytes+done,count);
        else if(p) memcpy(bytes+done,p->bytes+index,count);
        else memset(bytes+done,0,count);
        done+=count;
    }
    if(writing && pos+n>i->size) i->size=pos+n;
    if(!positional) f->pos=pos+n;
    return n;
}
long __bennett_rt_vfs_seek(struct bennett_file *f,int64_t offset,int whence,uint64_t block_size) {
    if(f->kind==BRT_NULL || f->kind==BRT_ZERO) return 0;
    if(f->kind!=BRT_REGULAR && f->kind!=BRT_DIRECTORY && f->kind!=BRT_BLOCK) return -ESPIPE;
    if(whence<0 || whence>2) return -EINVAL;
    uint64_t base=whence==0?0:whence==1?f->pos:f->kind==BRT_BLOCK?block_size:f->inode->size;
    uint64_t magnitude=offset<0?0-(uint64_t)offset:(uint64_t)offset;
    if(offset<0 ? magnitude>base : magnitude>(uint64_t)LONG_MAX-base) return -EINVAL;
    f->pos=offset<0?base-magnitude:base+magnitude; return f->pos;
}
static long fill_stat(struct bennett_inode *i,struct stat *st) {
    if(!st) return -EFAULT;
    memset(st,0,sizeof *st); st->st_dev=1; st->st_ino=i->number; st->st_nlink=i->links;
    st->st_mode=i->mode; st->st_size=i->size; st->st_blksize=4096;
    for(struct page *p=i->pages;p;p=p->next) st->st_blocks+=8;
    return 0;
}
long __bennett_rt_vfs_meta(long nr,long a,long b,long c,long d,long e) {
    (void)e;
    if(nr==5) {
        struct bennett_file *f=__bennett_rt_vfs_file(a); if(!f) return -EBADF;
        if(f->inode) return fill_stat(f->inode,(void *)b);
        struct bennett_inode device={f->kind,S_IFCHR|0666,0,0,1,0}; return fill_stat(&device,(void *)b);
    }
    if(nr==72) {
        struct descriptor *fd=descriptor(a); if(!fd || !fd->file) return -EBADF;
        if(b==F_DUPFD || b==F_DUPFD_CLOEXEC) return __bennett_rt_vfs_dup(a,c,0,b==F_DUPFD_CLOEXEC);
        if(b==F_GETFD) return fd->cloexec?FD_CLOEXEC:0;
        if(b==F_SETFD) { fd->cloexec=!!(c&FD_CLOEXEC); return 0; }
        if(b==F_GETFL) return fd->file->flags;
        if(b==F_SETFL) { fd->file->flags=(fd->file->flags&~(O_APPEND|O_NONBLOCK))|(c&(O_APPEND|O_NONBLOCK)); return 0; }
        return -EINVAL;
    }
    if(nr==74 || nr==75 || nr==77) {
        struct bennett_file *f=__bennett_rt_vfs_file(a); if(!f) return -EBADF;
        if(nr!=77) return f->kind==BRT_REGULAR?0:-EINVAL;
        if(f->kind!=BRT_REGULAR) return -EINVAL;
        if((f->flags&O_ACCMODE)==O_RDONLY) return -EBADF;
        if(b<0) return -EINVAL; truncate_inode(f->inode,b); return 0;
    }
    if(nr==79) {
        size_t n=strlen(cwd)+1; if((uint64_t)b<n) return -ERANGE;
        if(!a) return -EFAULT; memcpy((void *)a,cwd,n); return n;
    }
    if(nr==81) {
        struct bennett_file *f=__bennett_rt_vfs_file(a); if(!f) return -EBADF;
        if(f->kind!=BRT_DIRECTORY) return -ENOTDIR;
        for(struct name *n=names;n;n=n->next) if(n->inode==f->inode) { cwd=strdup(n->path); return 0; }
        return -ENOENT;
    }
    long at=AT_FDCWD; const char *path=(void *)a;
    if(nr==257 || nr==262 || nr==269 || nr==258 || nr==263) { at=a; path=(void *)b; }
    int error=0; char *full=canonical(path,at,&error); if(!full) return -error;
    struct name *n=lookup(full); long result=0;
    if(nr==83 || nr==258) {
        if(n) result=-EEXIST;
        else if((result=parent_directory(full))==0) {
            create_name(full,S_IFDIR|((nr==83?b:c)&07777),BRT_DIRECTORY); full=0;
        }
    } else if(!n) result=-ENOENT;
    else if(nr==4 || nr==6 || nr==262) result=fill_stat(n->inode,(void *)(nr==262?c:b));
    else if(nr==21 || nr==269) {
        long mode=nr==21?b:c;
        if(mode&~7L) result=-EINVAL;
        else if((mode&X_OK) && n->inode->kind!=BRT_DIRECTORY && !(n->inode->mode&0111)) result=-EACCES;
    } else if(nr==76) {
        if(b<0) result=-EINVAL;
        else if(n->inode->kind!=BRT_REGULAR) result=-EISDIR;
        else truncate_inode(n->inode,b);
    } else if(nr==80) {
        if(n->inode->kind!=BRT_DIRECTORY) result=-ENOTDIR; else cwd=strdup(full);
    } else if(nr==87 || nr==84 || nr==263) {
        int directory=nr==84 || (nr==263 && (c&AT_REMOVEDIR));
        if(directory && n->inode->kind!=BRT_DIRECTORY) result=-ENOTDIR;
        else if(!directory && n->inode->kind==BRT_DIRECTORY) result=-EISDIR;
        else if(!strcmp(full,"/")) result=-EBUSY;
        else {
            size_t len=strlen(full);
            if(directory) for(struct name *other=names;other;other=other->next)
                if(other->inode && !strncmp(other->path,full,len) && other->path[len]=='/') result=-ENOTEMPTY;
            if(!result) { --n->inode->links; n->inode=0; }
        }
    } else if(nr==82) {
        char *dest=canonical((void *)b,AT_FDCWD,&error);
        if(!dest) result=-error;
        else if(!strcmp(full,dest)) result=0;
        else if(!strcmp(full,"/")) result=-EBUSY;
        else if(n->inode->kind==BRT_DIRECTORY && !strncmp(dest,full,strlen(full)) && dest[strlen(full)]=='/') result=-EINVAL;
        else if((result=parent_directory(dest))==0) {
            struct name *existing=lookup(dest);
            if(existing && existing->inode->kind==BRT_DIRECTORY && n->inode->kind!=BRT_DIRECTORY) result=-EISDIR;
            else if(existing && existing->inode->kind!=BRT_DIRECTORY && n->inode->kind==BRT_DIRECTORY) result=-ENOTDIR;
            else {
                size_t oldlen=strlen(full);
                if(existing && existing->inode->kind==BRT_DIRECTORY) {
                    size_t len=strlen(dest);
                    for(struct name *child=names;child;child=child->next) if(child->inode &&
                        !strncmp(child->path,dest,len) && child->path[len]=='/') result=-ENOTEMPTY;
                }
                if(result) { free(dest); free(full); return result; }
                if(existing) { --existing->inode->links; existing->inode=0; }
                if(n->inode->kind==BRT_DIRECTORY) {
                    for(struct name *child=names;child;child=child->next) if(child->inode &&
                        !strncmp(child->path,full,oldlen) && child->path[oldlen]=='/') {
                        size_t prefix=strlen(dest), tail=strlen(child->path+oldlen)+1;
                        char *updated=malloc(prefix+tail);
                        memcpy(updated,dest,prefix); memcpy(updated+prefix,child->path+oldlen,tail); child->path=updated;
                    }
                    if(!strncmp(cwd,full,oldlen) && (!cwd[oldlen] || cwd[oldlen]=='/')) {
                        size_t prefix=strlen(dest), tail=strlen(cwd+oldlen)+1;
                        char *updated=malloc(prefix+tail);
                        memcpy(updated,dest,prefix); memcpy(updated+prefix,cwd+oldlen,tail); cwd=updated;
                    }
                }
                n->path=dest; dest=0;
            }
        }
        free(dest);
    } else if(nr==90) n->inode->mode=(n->inode->mode&S_IFMT)|(b&07777);
    else result=-ENOSYS;
    free(full); (void)d; return result;
}
