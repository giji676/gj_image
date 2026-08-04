.text
.global bmp_open_asm
# %rdi, %rsi, %rdx, %rcx, %r8, and %r9
# struct image_file {
#     const char *filename; // 0
#     int *width;           // 8
#     int *height;          // 16
#     int *channels;        // 24
# };

# struct __attribute__((packed)) bmp_file_header {
#     char signature[2];    // 0
#     uint32_t size;        // 2
#     uint16_t reserve1;    // 6
#     uint16_t reserve2;    // 8
#     uint32_t offset;      // 10
#                           // 14 total
# };

# struct __attribute__((packed)) bmp_bitmap_info_header {
#     uint32_t size;            // 0
#     int32_t width;            // 4
#     int32_t height;           // 8
#     uint16_t planes;          // 12
#     uint16_t bitCount;        // 14
#     uint32_t compression;     // 16
#     uint32_t sizeImage;       // 20
#     int32_t horizontalRes;    // 24
#     int32_t verticalRes;      // 28
#     uint32_t colorsUsed;      // 32
#     uint32_t colorsImportant; // 36
#                               // 40 total
# };

.equ FILE_HEADER_SIZE, 14
.equ BITMAP_INFO_HEADER_SIZE, 40

.equ WIDTH,           -4  # 4
.equ HEIGHT,          -8  # 4
.equ ABS_HEIGHT,      -12 # 4
.equ CHANNELS,        -16 # 4
.equ PIXELS,          -24 # 8
.equ ALL_DATA,        -32 # 8
.equ PADDED_ROW_SIZE, -36 # 4
.equ TOTAL_SIZE,      -40 # 4
.equ ACTUAL_ROW_SIZE, -44 # 4

# unsigned char *bmp_open_asm(struct image_file *);
bmp_open_asm:
    push %rbx
    push %r12
    push %r13
    push %r14

    push %rbp
    mov %rsp, %rbp

    mov (%rdi), %rbx   # save *filename into rbx
    mov %rdi, %r14

    mov $2, %rax       # 2 - "open" syscall
    mov %rbx, %rdi     # arg 1: path
    xor %rsi, %rsi     # arg 2: flsgs (0 = O_RDONLY)
    syscall

    test %rax, %rax    # check if it returned 0
    js err_file_open   # if < 0

    mov %rax, %r12     # save file descriptor to r12 (callee-save)
    # read bmp_file_header

    sub $16, %rsp      # reserve 16 bytes of memory

    mov %r12, %rdi     # arg 1: fd
    lea -16(%rbp), %rsi     # arg 2: bmp_file_header pointer
    call bmp_parse_file_header_asm

    test %rax, %rax
    jle err_file_read

    mov -16(%rbp), %ax  # load 2 bytes, zero extend to 64-bit
    cmp $0x4d42, %ax    # compare with 'B' 'M' signature (little endian)
    jne err_not_bmp

    # read bmp_bitmap_info_header
    sub $48, %rsp      # reserve 48 bytes of memory

    mov %r12, %rdi      # arg 1: fd
    lea -64(%rbp), %rsi
    call bmp_parse_bitmap_info_header_asm

    test %rax, %rax
    jle err_file_read

    mov $8, %rax        # 8 - "lseek" syscall
    mov %r12, %rdi      # arg 1: fd
    mov -6(%rbp), %rsi # arg 2: offset
    mov $0, %rdx        # arg 3: origin
    syscall

    mov %r12, %rdi      # arg 1: fd
    lea -16(%rbp), %rsi # arg 2: bmp_file_header
    lea -64(%rbp), %rdx # arg 3: bmp_bitmap_info_header
    push %rdx
    sub $8, %rsp        # for byte alignment
    call bmp_parse_pixels_asm
    add $8, %rsp
    pop %rdx
    mov %rax, %rbx      # save pixel pointer

    mov 8(%r14), %rax   # load width pointer
    # *image_file->width = value
    # -72 + 4 = +68
    mov 4(%rdx), %ecx   # load width value
    mov %ecx, (%rax)    # store through pointer

    mov 8(%rdx), %eax
    mov %eax, %ecx
    neg %eax
    cmovl %ecx, %eax        # abs(height)
    mov 16(%r14), %rcx      # load width pointer
    mov %eax, (%rcx)        # store through pointer

    # 8=3, 24=3, 32=4
    movzwl 14(%rdx), %eax      # load bitcount
check_8_:
    cmp $8, %ax
    jne check_24_
    mov $3, %ecx
    jmp continue_
check_24_:
    cmp $24, %ax
    jne check_32_
    mov $3, %ecx
    jmp continue_
check_32_:
    cmp $32, %ax
    jne default_
    mov $4, %ecx
    jmp continue_
default_:
    lea msg_unsupported_bitcount(%rip), %rdi
    xor %eax, %eax
    call printf
    xor %eax, %eax
    jmp cleanup
continue_:
    mov 24(%r14), %rax      # load width pointer
    mov %ecx, (%rax)        # store through pointer

    mov $3, %rax       # 3 - "close" syscall
    mov $2, %rdi       # arg 1: fd - 2 - stderr
    syscall

    mov %rbx, %rax # moved saved pixel pointer to return %rax

    jmp cleanup

cleanup:
    mov %rbp, %rsp
    pop %rbp

    pop %r14
    pop %r13
    pop %r12
    pop %rbx

    ret

# int bmp_parse_file_header(FILE *, struct bmp_file_header *)
bmp_parse_file_header_asm:
    push %rbp
    mov %rsp, %rbp

    xor %rax, %rax # read
    # %rdi         # file descriptor
    # %rsi         # address of buffer
    mov $14, %rdx  # size of buffer
    syscall

    mov %rbp, %rsp
    pop %rbp
    ret

# int bmp_parse_bitmap_info_header(FILE *, struct bmp_bitmap_info_header *)
bmp_parse_bitmap_info_header_asm:
    push %rbp
    mov %rsp, %rbp

    xor %rax, %rax # read
    # %rdi         # file descriptor
    # %rsi         # address of buffer
    mov $40, %rdx  # size of buffer
    syscall

    cmpl $40, 0(%rsi)
    je correct_bitmap_header_size
    cmpl $124, 0(%rsi)
    jne err_bitmap_header_size

correct_bitmap_header_size:
    mov %rbp, %rsp
    pop %rbp
    ret

# unsigned char *bmp_parse_pixels_asm(FILE *,
#                struct bmp_file_header *,
#                struct bmp_bitmap_info_header *)
bmp_parse_pixels_asm:
    push %rbp
    mov %rsp, %rbp

    cmpl $0, 16(%rdx)          # header->compression =? 0
    jne err_compression

    # 8=3, 24=3, 32=4
    movzwl 14(%rdx), %eax      # load bitcount
check_8:
    cmp $8, %ax
    jne check_24
    mov $3, %ecx
    jmp continue
check_24:
    cmp $24, %ax
    jne check_32
    mov $3, %ecx
    jmp continue
check_32:
    cmp $32, %ax
    jne default
    mov $4, %ecx
    jmp continue
default:
    lea msg_unsupported_bitcount(%rip), %rdi
    xor %eax, %eax
    call printf
    xor %eax, %eax
    jmp cleanup
continue:
    sub $48, %rsp      # reserve 48 bytes of memory for variables
    mov %ecx, CHANNELS(%rbp)

    mov 4(%rdx), %eax
    mov %eax, WIDTH(%rbp)

    mov 8(%rdx), %eax
    mov %eax, HEIGHT(%rbp)

    mov %eax, %ecx
    neg %eax
    cmovl %ecx, %eax         # abs(height)
    mov %eax, ABS_HEIGHT(%rbp)

    sub $8, %rsp # % 16 byte alignment
    push %rdi
    push %rdx
    push %rsi
    imul WIDTH(%rbp), %eax   # width * height
    mov CHANNELS(%rbp), %edi
    imul %eax, %edi          # ^ * channels

    call malloc
    pop %rsi
    pop %rdx
    pop %rdi
    add $8, %rsp
    test %rax, %rax
    jz malloc_failed
    mov %rax, PIXELS(%rbp) # pointer to malloced pixel data

    movzwl 14(%rdx), %eax  # load bitcount
    cmp $8, %eax
    jne padd_row_v2
    mov WIDTH(%rbp), %eax  # (width + 3) & ~3
    add $3, %eax
    and $~3, %eax          # paddedRowSize
    jmp padd_row_cont
padd_row_v2:
    movzwl 14(%rdx), %eax  # load bitcount
    imul WIDTH(%rbp), %eax # ((header->bitCount * width + 31) / 32) * 4;

    add $31, %eax
    shr $5, %eax
    shl $2, %eax      # paddedRowSize

padd_row_cont:
    mov %eax, PADDED_ROW_SIZE(%rbp)

    imul ABS_HEIGHT(%rbp), %eax   # totalSize
    mov %eax, TOTAL_SIZE(%rbp)

    sub $8, %rsp
    push %rdi
    push %rdx
    push %rsi
    mov %eax, %edi
    call malloc
    pop %rsi
    pop %rdx
    pop %rdi
    add $8, %rsp
    test %rax, %rax
    # TODO: if malloc failed free previous malloc PIXELS(%rsp)
    jz malloc_failed
    mov %rax, ALL_DATA(%rbp) # pointer to malloced allData

    mov WIDTH(%rbp), %eax
    imul CHANNELS(%rbp), %eax   # actualRowSize
    mov %eax, ACTUAL_ROW_SIZE(%rbp)

    # palette[256][4] = 1024
    sub $1024, %rsp
    movzwl 14(%rdx), %eax       # load bitcount
    cmp $8, %eax
    jne skip_palette_load_v2

    mov 10(%rsi), %ecx # file_header->offset
    push %rsi

    mov $FILE_HEADER_SIZE, %esi # sizeof(bmp_file_header) + header->size
    add 0(%rdx), %esi           # paletteStart

    sub %esi, %ecx
    shr $2, %ecx       # paletteSize

    cmp $0, %ecx
    jle err_invalid_palette
    cmp $256, %ecx
    jg err_invalid_palette

    push %rdx
    push %rcx

    mov $8, %rax       # 8 - "lseek" syscall
                       # arg 1: fd
    # %rsi             # arg 2: offset - paletteStart
    mov $0, %rdx       # arg 3: origin
    syscall
    pop %rcx

    xor %rax, %rax     # read
                       # arg 1: fd
    lea 48(%rbp), %rsi # arg 2: address of buffer
    movslq %ecx, %rdx  # arg 3: size of buffer - paletteSize * 4
    shl $2, %rdx       # paletteSize * 4
    syscall

    cmp %rdx, %rax

    pop %rdx
    pop %rsi

    jne err_file_read

skip_palette_load_v2:
    push %rsi
    push %rdx

    mov $8, %rax          # 8 - "lseek" syscall
                          # arg 1: fd
    mov 10(%rsi), %rdx
    mov %edx, %rsi     # arg 2: offset
    mov $0, %rdx          # arg 3: origin
    syscall

    xor %rax, %rax     # read
                       # arg 1: fd
    mov ALL_DATA(%rbp), %rsi   # arg 2: address of buffer
    mov TOTAL_SIZE(%rbp), %edx # arg 3: size of buffer
    syscall

    pop %rdx
    pop %rsi

    mov TOTAL_SIZE(%rbp), %ecx
    cmp %rcx, %rax
    jne err_file_read

# CONT HERE
    xor %rcx, %rcx    # i = 0
loop_i:
    cmp ABS_HEIGHT(%rbp), %ecx
    jge end_loop_i

    cmpl $0, HEIGHT(%rbp)
    jg bottom_up
    movslq %ecx, %r9 # dstRow = i
    jmp dst_row
bottom_up:
    mov HEIGHT(%rbp), %r9d
    dec %r9d
    sub %ecx, %r9d # dstRow = (height - 1 - i)
    jmp dst_row
dst_row:
    mov PADDED_ROW_SIZE(%rbp), %r10d # paddedRowSize
    imul %ecx, %r10d                 # i * paddedRowSize
    add ALL_DATA(%rbp), %r10         # rowData = allData + i * paddedRowSize

    mov ACTUAL_ROW_SIZE(%rbp), %r11d # actualRowSize
    imul %r9d, %r11d                 # dstRow * actualRowSize
    add PIXELS(%rbp), %r11           # dst = pixels + dstRow

    cmpw $8, 14(%rdx) # if (header->bitCount == 8)
    je bitcount_8
    cmpl $3, CHANNELS(%rbp)
    je channels_3
    cmpl $4, CHANNELS(%rbp)
    je channels_4

bitcount_8:
    push %r12
    xor %r8, %r8 # j = 0
pixel_loop_8:
    cmpl WIDTH(%rbp), %r8d
    jge pixel_done_8
    movzbl (%r10), %r12d # idx = *rowData
    inc %r10             # rowData++

    movb 50(%rbp,%r12,4), %al   # palette[idx][2]
    movb %al, (%r11)
    inc %r11

    movb 49(%rbp,%r12,4), %al   # palette[idx][1]
    movb %al, (%r11)
    inc %r11

    movb 48(%rbp,%r12,4), %al   # palette[idx][0]
    movb %al, (%r11)
    inc %r11

    inc %r8 # j++
    jmp pixel_loop_8

channels_3:
    xor %r8, %r8 # j = 0
pixel_loop_3:
    cmpl WIDTH(%rbp), %r8d
    jge pixel_done_3

    movb 2(%r10), %al
    movb %al, (%r11)
    inc %r11

    movb 1(%r10), %al
    movb %al, (%r11)
    inc %r11

    movb 0(%r10), %al
    movb %al, (%r11)
    inc %r11

    add $3, %r10
    inc %r8 # j++
    jmp pixel_loop_3

channels_4:
    xor %rdi, %rdi # j = 0
pixel_loop_4:
    cmpl WIDTH(%rbp), %r8d
    jge pixel_done_3

    # load BGRA into RGBA
    movb 2(%r10), %al
    movb %al, (%r11)
    inc %r11

    movb 1(%r10), %al
    movb %al, (%r11)
    inc %r11

    movb 0(%r10), %al
    movb %al, (%r11)
    inc %r11

    movb 3(%r10), %al
    movb %al, (%r11)
    inc %r11

    add $4, %r10
    inc %r8 # j++
    jmp pixel_loop_4

pixel_done_8:
    pop %r12
pixel_done_3:
pixel_done_4:
cont_loop_i:
    inc %ecx
    jmp loop_i

end_loop_i:
    mov ALL_DATA(%rbp), %rdi
    call free

    mov PIXELS(%rbp), %rax
    mov %rbp, %rsp
    pop %rbp
    ret

err_file_open:
err_file_read:
err_bitmap_header_size:
err_compression:
err_not_bmp:
err_invalid_palette:
malloc_failed:
    mov $60, %rax
    mov $-1, %rdi
    syscall

.section .rodata
print_uint:
    .string "print_int: %u\n" # 14 bytes
print_int:
    .string "print_int: %d\n" # 14 bytes
msg_err_file_open:
    .string "error opening the file!\n" # 24 bytes
msg_not_bmp:
    .string "error not a bmp file!\n" # 22 bytes
msg_err_read:
    .string "error reading the file!\n" # 24 bytes
msg_err_compression:
    .string "error compresed bmp not supported!\n" # 35 bytes
msg_unsupported_bitmap_header_size:
    .string "error unsuported bmp header type!\n" # 34 bytes
msg_malloc_failed:
    .string "error malloc failed!\n" # 21 bytes
msg_unsupported_bitcount:
    .string "error bitcount %d not yet supported!\n" # 37 bytes

.section ".note.GNU-stack"

