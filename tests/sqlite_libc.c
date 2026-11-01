#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <time.h>
extern void *__bennett_rt_alloc(uint64_t);
extern void __bennett_nz_free(void *);
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
struct calendar_case { int64_t t; int year,mon,day,hour,min,sec,wday,yday; };
#include "calendar_vectors.h"
int main(int argc,char **argv) {
    if (argc>1 && argv[1][0]=='f') {
        char *p=__bennett_rt_alloc(3); p[0]='a';p[1]='b';p[2]='c';
        return (int)strlen(p); /* no terminator: materialised memory fault 7 */
    }
    for (unsigned n=0;n<32;n++) for (unsigned offset=0;offset<8;offset++) {
        char *base=__bennett_rt_alloc(n+offset+1), *p=base+offset;
        static char copy[64];
        for (unsigned i=0;i<n;i++) p[i]='a'+i%26;
        p[n]=0;
        CHECK(strlen(p)==n && strnlen(p,SIZE_MAX)==n);
        memcpy(copy,p,n+1);
        CHECK(bcmp(p,copy,n+1)==0 && strcmp(p,copy)==0 && strncmp(p,copy,SIZE_MAX)==0);
        CHECK(strspn(p,"ab")== (n<2?n:2));
        CHECK(strspn(p,"")==0 && strcspn(p,"")==n);
        CHECK(strcspn(p,"cd")== (n<2?n:2));
        CHECK(strrchr(p,0)==p+n);
        CHECK(strrchr(p,'a')==(n ? p+((n-1)/26)*26 : 0));
        if (n) { copy[0]='X'; CHECK(bcmp(p,copy,n+1)!=0); }
        __bennett_nz_free(base);
    }
    for (size_t i=0;i<sizeof calendars/sizeof calendars[0];i++) {
        const struct calendar_case *v=calendars+i;
        time_t t=v->t; struct tm *r=localtime(&t);
        CHECK(r && r->tm_year==v->year-1900 && r->tm_mon==v->mon-1 && r->tm_mday==v->day);
        CHECK(r->tm_hour==v->hour && r->tm_min==v->min && r->tm_sec==v->sec);
        CHECK(r->tm_wday==v->wday && r->tm_yday==v->yday);
        CHECK(r->tm_isdst==0 && r->tm_gmtoff==0 && !strcmp(r->tm_zone,"UTC"));
        struct tm copy;
        CHECK(localtime_r(&t,&copy)==&copy && copy.tm_year==r->tm_year && copy.tm_yday==r->tm_yday);
        CHECK(gmtime(&t)->tm_mday==r->tm_mday);
    }
    time_t huge=INT64_MAX; errno=0;
    CHECK(!localtime(&huge) && errno==EOVERFLOW);
    huge=INT64_MIN; errno=0;
    CHECK(!localtime(&huge) && errno==EOVERFLOW);
    return 0;
}
