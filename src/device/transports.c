/* Separate translation-unit boundaries until materialisation. Only these
 * device transports are replaced in the native comparison publication. */
#include <stdint.h>
extern void __bennett_effect_console_write(const void *,uint64_t);
extern void __bennett_effect_clock_read(void *);
extern void __bennett_effect_entropy_read(void *,uint64_t);
extern void __bennett_effect_block_read(uint64_t,void *);
extern void __bennett_effect_block_write(uint64_t,const void *);
extern void __bennett_rt_exit(int);
long __bennett_rt_transport_console_write(long fd,const void *p,uint64_t n) {
    (void)fd; if(n) __bennett_effect_console_write(p,n); return n;
}
void __bennett_rt_transport_clock_read(void *p) { __bennett_effect_clock_read(p); }
void __bennett_rt_transport_entropy_read(void *p,uint64_t n) { __bennett_effect_entropy_read(p,n); }
void __bennett_rt_transport_block_read(uint64_t s,void *p) { __bennett_effect_block_read(s,p); }
void __bennett_rt_transport_block_write(uint64_t s,const void *p) { __bennett_effect_block_write(s,p); }
void __bennett_rt_transport_exit(int status) { __bennett_rt_exit(status); }
