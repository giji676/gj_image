#include <stddef.h>
#include <stdint.h>

uint32_t adler32_simd_(  /* SSSE3 */
    uint32_t adler,
    const unsigned char *buf,
    size_t len);
