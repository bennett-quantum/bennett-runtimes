/* Test-only effect materialisation, never a production marker definition. */
#include <stdint.h>
extern void __bennett_dev_clock_read(void *);
extern void __bennett_dev_entropy_read(void *,uint64_t);
extern void __bennett_dev_block_read(uint64_t,void *);
extern void __bennett_dev_block_write(uint64_t,const void *);
void __bennett_effect_clock_read(void *p) { __bennett_dev_clock_read(p); }
void __bennett_effect_entropy_read(void *p,uint64_t n) { __bennett_dev_entropy_read(p,n); }
void __bennett_effect_block_read(uint64_t s,void *p) { __bennett_dev_block_read(s,p); }
void __bennett_effect_block_write(uint64_t s,const void *p) { __bennett_dev_block_write(s,p); }
