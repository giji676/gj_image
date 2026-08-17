.text
.global paeth_predictor_asm
.global paeth_simd__m128i

# __m128i paeth_simd__m128i(uint64_t a, uint64_t b, uint64_t c);
paeth_simd__m128i:
    movq %rdi, %xmm0
    movq %rsi, %xmm1
    movq %rdx, %xmm2

    movdqa %xmm0, %xmm4
    paddw  %xmm1, %xmm4       # xmm4 = A+B

    movdqa %xmm2, %xmm5
    paddw  %xmm2, %xmm5       # xmm5 = 2C
    paddw  %xmm2, %xmm5       # xmm5 = 3C

    psubw  %xmm4, %xmm5       # xmm5 = 3C-(A+B)

    movdqa %xmm0, %xmm3
    pminsw %xmm1, %xmm3       # xmm3 = min(A, B)

    movdqa %xmm0, %xmm4
    pmaxsw %xmm1, %xmm4       # xmm4 = max(A, B)

    # xmm0 = A
    # xmm1 = B
    # xmm2 = C
    # xmm3 = LO
    # xmm4 = HI
    # xmm5 = THRESH

    # mask = (HI <= THRESH)
    movdqa  %xmm4, %xmm6
    pcmpgtw %xmm5, %xmm6       # xmm6 = HI > THRESH

    pcmpeqw %xmm8, %xmm8       # xmm8 = 0xffff...
    pxor    %xmm8, %xmm6       # xmm6 = ~(HI > THRESH)
                                #       = HI <= THRESH

    # t0 = (mask & LO) | (~mask & C)

    movdqa  %xmm6, %xmm7
    pand    %xmm3, %xmm7       # xmm7 = mask & LO

    movdqa  %xmm6, %xmm8
    pandn   %xmm2, %xmm8       # xmm8 = (~mask) & C

    por     %xmm8, %xmm7       # xmm7 = t0

    # xmm6 = mask = (THRESH <= LO)
    movdqa  %xmm5, %xmm6
    pcmpgtw %xmm3, %xmm6       # THRESH > LO
    pcmpeqw %xmm8, %xmm8       # xmm8 = 0xffff...
    pxor    %xmm8, %xmm6       # xmm6 = THRESH <= LO

    # result = (mask & HI) | (~mask & T0)
    movdqa  %xmm6, %xmm8
    pand    %xmm4, %xmm8       # mask & HI

    movdqa  %xmm6, %xmm0
    pandn   %xmm7, %xmm0       # (~mask) & T0

    por     %xmm8, %xmm0       # result

    ret
