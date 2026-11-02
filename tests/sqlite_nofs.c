/* Application-selected memory-only VFS from the SQLite spike's documented
 * OS_OTHER profile. It has no disk volume; the runtime's general VFS/device
 * capability remains separate. This file does not replace SQLite's memdb. */
#include "sqlite3.h"
#include <string.h>
static int no_open(sqlite3_vfs *v,const char *name,sqlite3_file *f,int flags,int *out) {
    (void)v;(void)name;(void)flags;(void)out; f->pMethods=0; return SQLITE_CANTOPEN;
}
static int no_delete(sqlite3_vfs *v,const char *name,int sync) {
    (void)v;(void)name;(void)sync; return SQLITE_IOERR_DELETE;
}
static int no_access(sqlite3_vfs *v,const char *name,int flags,int *out) {
    (void)v;(void)name;(void)flags; *out=0; return SQLITE_OK;
}
static int path(sqlite3_vfs *v,const char *name,int n,char *out) {
    (void)v; size_t len=strlen(name);
    if (n<=0 || len>=(size_t)n) return SQLITE_CANTOPEN;
    memcpy(out,name,len+1); return SQLITE_OK;
}
static int random_bytes(sqlite3_vfs *v,int n,char *out) {
    (void)v; for (int i=0;i<n;i++) out[i]=(char)(i*37+11); return n;
}
static int sleep_fixed(sqlite3_vfs *v,int n) { (void)v; return n; }
static int time_fixed(sqlite3_vfs *v,double *out) { (void)v; *out=2440587.5; return SQLITE_OK; }
static sqlite3_vfs memory_profile={
    .iVersion=1,.szOsFile=sizeof(sqlite3_file),.mxPathname=1024,.zName="bennett-nofs",
    .xOpen=no_open,.xDelete=no_delete,.xAccess=no_access,.xFullPathname=path,
    .xRandomness=random_bytes,.xSleep=sleep_fixed,.xCurrentTime=time_fixed
};
int sqlite3_os_init(void) { return sqlite3_vfs_register(&memory_profile,1); }
int sqlite3_os_end(void) { return SQLITE_OK; }
