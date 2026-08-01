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

# unsigned char *bmp_open_asm(struct image_file *);
bmp_open_asm:
    push %rbx
    push %r12
    push %r13
    push %r14

    mov (%rdi), %rbx # save *filename into rbx

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

    lea print_uint(%rip), %rdi # arg 1: format string
    mov 2(%rsp), %esi          # arg 2: buffer
    xor %eax, %eax
    call printf                # print size

    lea print_uint(%rip), %rdi # arg 1: format string
    mov 10(%rsp), %esi         # arg 2: buffer
    xor %eax, %eax
    call printf                # print offset

    # read bmp_bitmap_info_header
    add $48, %r13      # keep track of total reserved (for cleanup)
    sub $48, %rsp      # reserve 48 bytes of memory

    xor %rax, %rax     # read
    mov %r12, %rdi     # file descriptor
    mov %rsp, %rsi     # address of buffer
    mov $48, %rdx      # size of buffer
    syscall

    test %rax, %rax
    jle err_file_read

    lea print_int(%rip), %rdi # arg 1: format string
    mov 0(%rsp), %esi         # arg 2: buffer
    xor %eax, %eax
    call printf               # print size

    lea print_int(%rip), %rdi # arg 1: format string
    mov 4(%rsp), %esi         # arg 2: buffer
    xor %eax, %eax
    call printf               # print width

    lea print_int(%rip), %rdi # arg 1: format string
    mov 8(%rsp), %esi         # arg 2: buffer
    xor %eax, %eax
    call printf               # print height

    cmpl $0, 16(%rsp)          # header->compression =? 0
    jne err_compression

    mov $8, %rax       # 8 - "lseek" syscall
    mov %r12, %rdi     # arg 1: fd
    mov 10(%rsp), %rsi # arg 2: offset
    mov $0, %rdx       # arg 3: origin
    syscall

    lea print_uint(%rip), %rdi # arg 1: format string
    movzwl 14(%rsp), %esi      # arg 2: buffer
    xor %eax, %eax
    call printf                # print bitcount

    # 8=3, 24=3, 32=4
    movzwl 14(%rsp), %esi      # load bitcount
check8:
    cmp $8, %si
    jne check24
    mov $3, %edx
    jmp continue
check24:
    cmp $24, %si
    jne check32
    mov $3, %edx
    jmp continue
check32:
    cmp $32, %si
    jne default
    mov $4, %edx
    jmp continue
default:
    # err
continue:
    mov 8(%rsp), %eax # height
    mov %eax, %ebx
    neg %eax
    cmovl %ebx, %eax  # abs(height)

    imul 4(%rsp), %eax # width * height
    imul %eax, %edx    # ^ * channels

    # lea print_uint(%rip), %rdi # arg 1: format string
    # mov %edx, %esi             # arg 2: buffer
    # xor %eax, %eax
    # call printf                # print size

    mov %edx, %edi
    call malloc
    test %rax, %rax
    jz malloc_failed
    mov %rax, %r14    # pointer to malloced pixel data

    add %r13, %rsp    # restore aligment (total that was reserved)

    mov %r14, %rax    # moved saved pixel pointer to return %rax
    jmp cleanup

malloc_failed:
    mov $1, %rax       # 1 - "write" syscall
    mov $2, %rdi       # arg 1: fg - 2 - stderr
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

.section ".note.GNU-stack"

