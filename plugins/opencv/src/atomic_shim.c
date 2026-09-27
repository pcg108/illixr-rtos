#include <stdint.h>

/* GCC's byte-exchange ABI. SDK GCC 12 calls this helper; RV64 only has
 * word/doubleword LR/SC. Preserve adjacent bytes and use seq-cst ordering
 * (also valid for every weaker model requested by callers). */
unsigned char __atomic_exchange_1(volatile void *ptr, unsigned char value, int model)
{
    (void)model;
    uintptr_t address = (uintptr_t)ptr;
    volatile uint32_t *word = (volatile uint32_t *)(address & ~(uintptr_t)3);
    unsigned shift = (unsigned)(address & 3) * 8;
    uint32_t mask = UINT32_C(255) << shift;
    uint32_t expected = __atomic_load_n(word, __ATOMIC_SEQ_CST);
    for (;;) {
        uint32_t desired = (expected & ~mask) | ((uint32_t)value << shift);
        if (__atomic_compare_exchange_n(word, &expected, desired, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            return (unsigned char)(expected >> shift);
        }
    }
}
