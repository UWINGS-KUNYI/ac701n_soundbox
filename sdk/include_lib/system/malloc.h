#ifndef _MEM_HEAP_H_
#define _MEM_HEAP_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    P_MEMORY_TOTAL,       // ram0
    P_MEMORY_UNUSED,      // ram0
    P_MEMORY_USED,        // ram0

    P_VLT_MEMORY_TOTAL,   // ram1
    P_VLT_MEMORY_UNUSED,  // ram1
    P_VLT_MEMORY_USED,    // ram1

    P_HEAP_SIZE,          // ram0 + ram1
} MEMORY_TYPE;

extern void *malloc(size_t size);
extern void *zalloc(size_t size);
extern void *calloc(size_t count, size_t size);
extern void *realloc(void *rmem, size_t newsize);
extern void  free(void *mem);


extern void *kmalloc(size_t size, int flags);
extern void *vmalloc(size_t size);
extern void vfree(void *addr);
extern void *kzalloc(unsigned int len, int a);
extern void kfree(void *p);

extern void malloc_stats(void);

extern void malloc_dump();

void memory_init(void);

void mem_stats(void);

/* ---------------------------------------------------------------------------- */
/**
 * @brief :获取物理内存的大小
 *
 * @param type: 需要获取的内存类型;
 *
 * @return : 对应类型物理内存大小
 */
/* ---------------------------------------------------------------------------- */
size_t memory_get_size(MEMORY_TYPE type);


#ifdef __cplusplus
}
#endif

#endif /* _MEM_HEAP_H_ */
