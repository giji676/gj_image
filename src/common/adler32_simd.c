/*
 * Copyright 2015 The Chromium Authors
 *
 * Modifications Copyright (c) 2026 giji676
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Google LLC nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "adler32_simd.h"

/* Definitions from adler32.c: largest prime smaller than 65536 */
#define BASE 65521U

/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */
#define NMAX 5552

// TODO: Add non-SSE and implementations?
#define ADLER32_SIMD_SSSE3
#if defined(ADLER32_SIMD_SSSE3)

#include <tmmintrin.h>

#if defined(__GNUC__)
__attribute__((__target__("ssse3")))
#endif
uint32_t adler32_simd_(  /* SSSE3 */
    uint32_t adler,
    const unsigned char *buf,
    size_t len)
{
    /*
     * Split Adler-32 into component sums.
     */
    uint32_t s1 = adler & 0xffff;
    uint32_t s2 = adler >> 16;
    /*
     * Process the data in blocks.
     */
    const unsigned BLOCK_SIZE = 1 << 5;
    size_t blocks = len / BLOCK_SIZE;
    len -= blocks * BLOCK_SIZE;
    while (blocks)
    {
        unsigned n = NMAX / BLOCK_SIZE;  /* The NMAX constraint. */
        if (n > blocks)
            n = (unsigned) blocks;
        blocks -= n;
        const __m128i tap1 =
            _mm_setr_epi8(32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17);
        const __m128i tap2 =
            _mm_setr_epi8(16,15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1);
        const __m128i zero =
            _mm_setr_epi8( 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        const __m128i ones =
            _mm_set_epi16( 1, 1, 1, 1, 1, 1, 1, 1);
        /*
         * Process n blocks of data. At most NMAX data bytes can be
         * processed before s2 must be reduced modulo BASE.
         */
        __m128i v_ps = _mm_set_epi32(0, 0, 0, s1 * n);
        __m128i v_s2 = _mm_set_epi32(0, 0, 0, s2);
        __m128i v_s1 = _mm_set_epi32(0, 0, 0, 0);
        do {
            /*
             * Load 32 input bytes.
             */
            const __m128i bytes1 = _mm_loadu_si128((__m128i*)(buf));
            const __m128i bytes2 = _mm_loadu_si128((__m128i*)(buf + 16));
            /*
             * Add previous block byte sum to v_ps.
             */
            v_ps = _mm_add_epi32(v_ps, v_s1);
            /*
             * Horizontally add the bytes for s1, multiply-adds the
             * bytes by [ 32, 31, 30, ... ] for s2.
             */
            v_s1 = _mm_add_epi32(v_s1, _mm_sad_epu8(bytes1, zero));
            const __m128i mad1 = _mm_maddubs_epi16(bytes1, tap1);
            v_s2 = _mm_add_epi32(v_s2, _mm_madd_epi16(mad1, ones));
            v_s1 = _mm_add_epi32(v_s1, _mm_sad_epu8(bytes2, zero));
            const __m128i mad2 = _mm_maddubs_epi16(bytes2, tap2);
            v_s2 = _mm_add_epi32(v_s2, _mm_madd_epi16(mad2, ones));
            buf += BLOCK_SIZE;
        } while (--n);
        v_s2 = _mm_add_epi32(v_s2, _mm_slli_epi32(v_ps, 5));
        /*
         * Sum epi32 ints v_s1(s2) and accumulate in s1(s2).
         */
#define S23O1 _MM_SHUFFLE(2,3,0,1)  /* A B C D -> B A D C */
#define S1O32 _MM_SHUFFLE(1,0,3,2)  /* A B C D -> C D A B */
        v_s1 = _mm_add_epi32(v_s1, _mm_shuffle_epi32(v_s1, S23O1));
        v_s1 = _mm_add_epi32(v_s1, _mm_shuffle_epi32(v_s1, S1O32));
        s1 += _mm_cvtsi128_si32(v_s1);
        v_s2 = _mm_add_epi32(v_s2, _mm_shuffle_epi32(v_s2, S23O1));
        v_s2 = _mm_add_epi32(v_s2, _mm_shuffle_epi32(v_s2, S1O32));
        s2 = _mm_cvtsi128_si32(v_s2);
#undef S23O1
#undef S1O32
        /*
         * Reduce.
         */
        s1 %= BASE;
        s2 %= BASE;
    }
    /*
     * Handle leftover data.
     */
    if (len) {
        if (len >= 16) {
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            len -= 16;
        }
        while (len--) {
            s2 += (s1 += *buf++);
        }
        if (s1 >= BASE)
            s1 -= BASE;
        s2 %= BASE;
    }
    /*
     * Return the recombined sums.
     */
    return s1 | (s2 << 16);
}
#endif
