#include "sqlite3.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"
static uint64_t image_hash(const unsigned char *bytes,size_t n) {
    const volatile unsigned char *p=bytes;
    uint64_t h=UINT64_C(14695981039346656037);
    for (size_t i=0;i<n;i++) h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}
static int error(sqlite3 *db,int code) {
    fputs(db?sqlite3_errmsg(db):"SQLite open failed",stderr); return code;
}
static int run_image(const unsigned char *bytes,uint64_t len,sqlite3_int64 id,int writing) {
    /* Every executable invocation starts from the static cold SQLite image. */
    sqlite3 *db=0;
    if (sqlite3_open(":memory:",&db)!=SQLITE_OK) return error(db,84);
    if (sqlite3_deserialize(db,"main",(unsigned char *)bytes,len,len,
                            SQLITE_DESERIALIZE_READONLY)!=SQLITE_OK) return error(db,85);
    if (writing) {
        int r=sqlite3_exec(db,"UPDATE people SET age=age+1 WHERE id=1",0,0,0);
        if (r!=SQLITE_READONLY) return error(db,86);
        if (sqlite3_close(db)!=SQLITE_OK) return 87;
        return 0;
    }
    static const char query[]=
        "SELECT 1 FROM people WHERE rowid = ?1 AND name LIKE '%an%' "
        "AND upper(substr(city, 1, 1)) IN ('B', 'L', 'M') "
        "AND age BETWEEN 25 AND 60 AND (score * 7 + age) % 11 = 3";
    sqlite3_stmt *stmt=0;
    if (sqlite3_prepare_v2(db,query,-1,&stmt,0)!=SQLITE_OK) return error(db,88);
    if (sqlite3_bind_int64(stmt,1,id)!=SQLITE_OK) return error(db,89);
    int r=sqlite3_step(stmt);
    if (r!=SQLITE_ROW && r!=SQLITE_DONE) return error(db,90);
    int match=r==SQLITE_ROW;
    if (sqlite3_finalize(stmt)!=SQLITE_OK || sqlite3_close(db)!=SQLITE_OK) return 91;
    return match;
}
/* The oracle-shaped interface takes caller-provided image bytes and length;
 * SQLite has no private global database or caller connection to reuse. */
uint8_t query_on_image(const unsigned char *bytes,uint64_t len,sqlite3_int64 id) {
    return (uint8_t)run_image(bytes,len,id,0);
}
int main(int argc,char **argv) {
    if (argc!=2) return 80;
    sqlite3_int64 id=0;
    int writing=!strcmp(argv[1],"write");
    if (!writing) for (const char *p=argv[1];*p;p++) {
        if (*p<'0' || *p>'9') return 81;
        id=id*10+(*p-'0'); if (id>IMAGE_ROWS+1) return 82;
    }
    const unsigned char *bytes=image;
    unsigned char *copy=0;
    /* Exercise two input addresses, including a writable unaligned copy.
     * The read-only test cannot rely on ELF page protection to pass. */
    if (writing || (id&1)) {
        copy=malloc(sizeof image+3);
        memcpy(copy+3,image,sizeof image); bytes=copy+3;
    }
    if (image_hash(bytes,sizeof image)!=IMAGE_HASH) return 83;
    int r=writing?run_image(bytes,IMAGE_SIZE,id,1):query_on_image(bytes,IMAGE_SIZE,id);
    if (image_hash(bytes,sizeof image)!=IMAGE_HASH || image_hash(image,sizeof image)!=IMAGE_HASH) return 92;
    free(copy);
    return r;
}
