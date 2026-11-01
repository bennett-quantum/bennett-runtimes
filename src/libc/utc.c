/* Deterministic UTC device profile, dialect spike SQLite §5.2(iv).
 * Calendar arithmetic is musl's unmodified __secs_to_tm; no clock, TZ file,
 * environment lookup, native libc or Linux timezone operation is consulted. */
#define _GNU_SOURCE
#include <errno.h>
#include <time.h>
extern int __secs_to_tm(long long,struct tm *);
static struct tm result;
static const char utc[]="UTC";
struct tm *localtime_r(const time_t *t,struct tm *out) {
    if (__secs_to_tm(*t,out)<0) { errno=EOVERFLOW; return 0; }
    out->tm_isdst=0; out->tm_gmtoff=0; out->tm_zone=utc;
    return out;
}
struct tm *localtime(const time_t *t) { return localtime_r(t,&result); }
struct tm *gmtime_r(const time_t *t,struct tm *out) { return localtime_r(t,out); }
struct tm *gmtime(const time_t *t) { return localtime(t); }
