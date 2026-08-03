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

.equ WIDTH,           0
.equ HEIGHT,          4
.equ ABS_HEIGHT,      8
.equ CHANNELS,        12
.equ PIXELS,          16
.equ ALL_DATA,        24
.equ PADDED_ROW_SIZE, 32
.equ TOTAL_SIZE,      36
.equ ACTUAL_ROW_SIZE, 40

# unsigned char *bmp_open_asm(struct image_file *);
bmp_open_asm:
    push %rbx
    push %r12
    push %r13
    push %r14

    mov (%rdi), %rbx # save *filename into rbx
    mov %rdi, %r14

    mov $2, %rax       # 2 - "open" syscall
    mov %rbx, %rdi     # arg 1: path
    xor %rsi, %rsi     # arg 2: flsgs (0 = O_RDONLY)
    syscall

    test %rax, %rax    # check if it returned 0
    js err_file_open   # if < 0

    mov %rax, %r12     # save file descriptor to r12 (callee-save)
    # read bmp_file_header
    mov $16, %r13      # keep track of total reserved (for cleanup)
    sub $16, %rsp      # reserve 16 bytes of memory

    xor %rax, %rax     # read
    mov %r12, %rdi     # file descriptor
    mov %rsp, %rsi     # address of buffer
    mov $14, %rdx      # size of buffer
    syscall

    test %rax, %rax
    jle err_file_read

    movzwq (%rsp), %rax # load 2 bytes, zero extend to 64-bit
    cmp $0x4d42, %ax    # compare with 'B' 'M' signature (little endian)
    jne not_bmp

    # lea print_uint(%rip), %rdi # arg 1: format string
    # mov 2(%rsp), %esi          # arg 2: buffer
    # xor %eax, %eax
    # call printf                # print size
    #
    # lea print_uint(%rip), %rdi # arg 1: format string
    # mov 10(%rsp), %esi         # arg 2: buffer
    # xor %eax, %eax
    # call printf                # print offset

    # read bmp_bitmap_info_header
    add $48, %r13      # keep track of total reserved (for cleanup)
    sub $48, %rsp      # reserve 48 bytes of memory

    xor %rax, %rax     # read
    mov %r12, %rdi     # file descriptor
    mov %rsp, %rsi     # address of buffer
    mov $40, %rdx      # size of buffer
    syscall

    test %rax, %rax
    jle err_file_read

    # lea print_int(%rip), %rdi # arg 1: format string
    # mov 0(%rsp), %esi         # arg 2: buffer
    # xor %eax, %eax
    # call printf               # print size
    #
    # lea print_int(%rip), %rdi # arg 1: format string
    # mov 4(%rsp), %esi         # arg 2: buffer
    # xor %eax, %eax
    # call printf               # print width
    #
    # lea print_int(%rip), %rdi # arg 1: format string
    # mov 8(%rsp), %esi         # arg 2: buffer
    # xor %eax, %eax
    # call printf               # print height

    cmpl $0, 16(%rsp)          # header->compression =? 0
    jne err_compression

    mov $8, %rax       # 8 - "lseek" syscall
    mov %r12, %rdi     # arg 1: fd
    mov 10(%rsp), %rsi # arg 2: offset
    mov $0, %rdx       # arg 3: origin
    syscall

    # lea print_uint(%rip), %rdi # arg 1: format string
    # movzwl 14(%rsp), %esi      # arg 2: buffer
    # xor %eax, %eax
    # call printf                # print bitcount

    # 8=3, 24=3, 32=4
    movzwl 14(%rsp), %esi      # load bitcount
    # TODO: use CMOVcc (conditional move?)
check_8:
    cmp $8, %si
    jne check_24
    mov $3, %ecx
    jmp continue
check_24:
    cmp $24, %si
    jne check_32
    mov $3, %ecx
    jmp continue
check_32:
    cmp $32, %si
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
    # %ecx = channels
    add $48, %r13      # keep track of total reserved (for cleanup)
    sub $48, %rsp      # reserve 40 bytes of memory for variables

    mov %ecx, CHANNELS(%rsp)

    mov 52(%rsp), %eax
    mov %eax, WIDTH(%rsp)

    mov 56(%rsp), %eax
    mov %eax, HEIGHT(%rsp)

    mov 56(%rsp), %eax # height
    mov %eax, %ebx
    neg %eax
    cmovl %ebx, %eax  # abs(height)

    mov %eax, ABS_HEIGHT(%rsp)

    imul WIDTH(%rsp), %eax # width * height
    mov CHANNELS(%rsp), %edi
    imul %eax, %edi    # ^ * channels

    call malloc
    test %rax, %rax
    jz malloc_failed
    mov %rax, PIXELS(%rsp)    # pointer to malloced pixel data

    movzwl 62(%rsp), %eax # load bitcount
    cmp $8, %eax
    jne padd_row_v2
    mov WIDTH(%rsp), %eax  # (width + 3) & ~3
    add $3, %eax
    and $~3, %eax      # paddedRowSize
    jmp padd_row_cont
padd_row_v2:
    # lea print_uint(%rip), %rdi # arg 1: format string
    # movzwl 62(%rsp), %esi      # arg 2: buffer
    # xor %eax, %eax
    # call printf                # print bitcount

    movzwl 62(%rsp), %eax # load bitcount
    imul WIDTH(%rsp), %eax # ((header->bitCount * width + 31) / 32) * 4;

    add $31, %eax
    shr $5, %eax
    shl $2, %eax      # paddedRowSize

padd_row_cont:
    mov %eax, PADDED_ROW_SIZE(%rsp)

    imul ABS_HEIGHT(%rsp), %eax   # totalSize

    mov %eax, TOTAL_SIZE(%rsp)

    mov %eax, %edi
    call malloc
    test %rax, %rax
    # TODO: if malloc failed free previous malloc PIXELS(%rsp)
    jz malloc_failed
    mov %rax, ALL_DATA(%rsp) # pointer to malloced allData

    mov WIDTH(%rsp), %eax
    imul CHANNELS(%rsp), %eax   # actualRowSize

    mov %eax, ACTUAL_ROW_SIZE(%rsp)

    movzwl 62(%rsp), %esi      # load bitcount
    cmp $8, %si
    jne skip_palette_load
    # TODO: if (header->bitCount == 8) { ... }
skip_palette_load:
    mov $8, %rax       # 8 - "lseek" syscall
    mov %r12, %rdi     # arg 1: fd
    # offset: 48+48+10 = 106
    mov 106(%rsp), %esi # arg 2: offset
    mov $0, %rdx       # arg 3: origin
    syscall

    xor %rax, %rax     # read
    mov %r12, %rdi     # file descriptor
    mov ALL_DATA(%rsp), %rsi     # address of buffer
    mov TOTAL_SIZE(%rsp), %rdx      # size of buffer
    syscall

    test %rax, %rax
    jle err_file_read # TODO: free malloced memory

    xor %rcx, %rcx    # i = 0

loop_i:
    cmp ABS_HEIGHT(%rsp), %ecx
    jge end_loop_i

    cmpl $0, HEIGHT(%rsp)
    jg bottom_up
    mov %ecx, %eax # dstRow = i
    jmp dst_row
bottom_up:
    mov HEIGHT(%rsp), %eax
    dec %eax
    sub %ecx, %eax # dstRow = (height - 1 - i)
    jmp dst_row
dst_row:
    movslq PADDED_ROW_SIZE(%rsp), %rsi
    imul %rcx, %rsi
    mov ALL_DATA(%rsp), %rdx
    add %rsi, %rdx # *rowData = allData + i * paddedRowSize

    movslq ACTUAL_ROW_SIZE(%rsp), %rsi
    movslq %eax, %rax
    imul %rax, %rsi
    add PIXELS(%rsp), %rsi # *dst = pixels + dstRow * actualRowSize

    cmpw $8, 62(%rsp) # if (header->bitCount == 8)
    je bitcount_8
    cmpl $3, CHANNELS(%rsp)
    je channels_3
    cmpl $4, CHANNELS(%rsp)
    je channels_4

bitcount_8:
    # TODO

channels_3:
    # *src = rowData = %rdx
    # *dst = %rsi
    xor %rdi, %rdi # j = 0
pixel_loop_3:
    cmpl WIDTH(%rsp), %edi
    jge pixel_done_3

    movb 2(%rdx), %al
    movb %al, (%rsi)
    inc %rsi

    movb 1(%rdx), %al
    movb %al, (%rsi)
    inc %rsi

    movb 0(%rdx), %al
    movb %al, (%rsi)
    inc %rsi

    add $3, %rdx
    inc %rdi # j++
    jmp pixel_loop_3

channels_4:
    # *src = rowData = %rdx
    # *dst = %rsi
    xor %rdi, %rdi # j = 0
pixel_loop_4:
    cmpl WIDTH(%rsp), %edi
    jge pixel_done_4

    movb 3(%rdx), %al
    movb %al, (%rsi)
    inc %rsi

    movb 2(%rdx), %al
    movb %al, (%rsi)
    inc %rsi

    movb 1(%rdx), %al
    movb %al, (%rsi)
    inc %rsi

    movb 0(%rdx), %al
    movb %al, (%rsi)
    inc %rsi

    add $4, %rdx
    inc %rdi # j++
    jmp pixel_loop_4

pixel_done_3:
pixel_done_4:
cont_loop_i:
    inc %ecx
    jmp loop_i

end_loop_i:
    mov ALL_DATA(%rsp), %rdi
    call free

    mov 8(%r14), %rax       # load width pointer
    # *image_file->width = value
    mov WIDTH(%rsp), %ecx  # load width value
    mov %ecx, (%rax)       # store through pointer

    mov 16(%r14), %rax       # load width pointer
    # *image_file->height = value
    mov ABS_HEIGHT(%rsp), %ecx  # load width value
    mov %ecx, (%rax)       # store through pointer

    mov 24(%r14), %rax       # load width pointer
    # *image_file->channels = value
    mov CHANNELS(%rsp), %ecx  # load width value
    mov %ecx, (%rax)       # store through pointer

    mov $3, %rax       # 3 - "close" syscall
    mov $2, %rdi       # arg 1: fd - 2 - stderr
    syscall

    mov PIXELS(%rsp), %rax # moved saved pixel pointer to return %rax
    add %r13, %rsp         # restore aligment (total that was reserved)
    jmp cleanup

malloc_failed:
    # TODO: close fd on any failures
    mov $1, %rax       # 1 - "write" syscall
    mov $2, %rdi       # arg 1: fd - 2 - stderr
    lea msg_not_bmp(%rip), %rsi # arg 2: buffer
    mov $22, %rdx      # arg 3: count
    syscall

    xor %eax, %eax    # return NULL
    jmp cleanup

not_bmp:
    add %r13, %rsp    # restore aligment (total that was reserved)

    mov $1, %rax       # 1 - "write" syscall
    mov $2, %rdi       # arg 1: fg - 2 - stderr
    lea msg_not_bmp(%rip), %rsi # arg 2: buffer
    mov $22, %rdx      # arg 3: count
    syscall

    xor %eax, %eax     # return NULL
    jmp cleanup

err_file_open:
    mov $1, %rax       # 1 - "write" syscall
    mov $2, %rdi       # arg 1: fg - 2 - stderr
    lea msg_err_file_open(%rip), %rsi # arg 2: buffer
    mov $24, %rdx      # arg 3: count
    syscall

    xor %eax, %eax     # return NULL
    jmp cleanup

err_file_read:
    add %r13, %rsp    # restore aligment (total that was reserved)

    mov $1, %rax       # 1 - "write" syscall
    mov $2, %rdi       # arg 1: fg - 2 - stderr
    lea msg_err_read(%rip), %rsi # arg 2: buffer
    mov $24, %rdx      # arg 3: count
    syscall

    xor %eax, %eax     # return NULL
    jmp cleanup

err_compression:
    add %r13, %rsp    # restore aligment (total that was reserved)

    mov $1, %rax       # 1 - "write" syscall
    mov $2, %rdi       # arg 1: fg - 2 - stderr
    lea msg_err_compression(%rip), %rsi # arg 2: buffer
    mov $35, %rdx      # arg 3: count
    syscall

    xor %eax, %eax     # return NULL
    jmp cleanup

cleanup:
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    ret

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
msg_malloc_failed:
    .string "error malloc failed!\n" # 21 bytes
msg_unsupported_bitcount:
    .string "error bitcount %d not yet supported!\n" # 37 bytes

.section ".note.GNU-stack"

