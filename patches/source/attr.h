#define __attribute_used__ __attribute__((used))
#define __attribute_data__ __attribute__((section(".data")))
#define __attribute_reloc__ __attribute__((section(".reloc")))
#define __attribute_aligned_data__ __attribute__((aligned(32), section(".data"))) 
#define countof(a) (sizeof(a)/sizeof(a[0]))
#define make_type(a,b,c,d) (((u32)a)<<24 | ((u32)b)<<16 | ((u32)c)<<8 | ((u32)d))
