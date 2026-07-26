#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/common.h"

unsigned char flip = 0;

void gj_vflip_image(unsigned char _flip) {
    flip = _flip;
}

// Reverse the lowest n bits of x
uint32_t reverse_bits(uint32_t x, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        r <<= 1;
        r |= (x >> i) & 1;
    }
    return r;
}

void print_binary(uint32_t value, int bits) {
    for (int i = bits - 1; i >= 0; i--) {
        printf("%c", (value & (1 << i)) ? '1' : '0');
    }
    putchar('\n');
}

void bitstream_init(struct bitStream *bs, uint8_t *data, size_t length) {
    bs->data = data;
    bs->bytepos = 0;
    bs->length = length;
}

int bitstream_read(struct bitStream *bs, int n, uint32_t *out) {
    if (n <= 0 || n > 32) return -1;

    /* Refill buffer until we have enough bits */
    while (bs->buffer_left < n) {
        if (bs->bytepos >= bs->length) {
            return -1; // out of input
        }

        /* Append next byte to buffer (LSB-first) */
        bs->bit_buffer |= ((uint32_t)bs->data[bs->bytepos++]) << bs->buffer_left;
        bs->buffer_left += 8;
    }

    /* Extract lowest n bits */
    *out = bs->bit_buffer & ((1u << n) - 1);

    /* Consume bits */
    bs->bit_buffer >>= n;
    bs->buffer_left -= n;

    return 0;
}

// Write n bits from 'in' to the bitstream (LSB first)
int bitstream_write(struct bitStream *bs, int n, uint32_t in) {
    if (n <= 0 || n > 32) return -1;

    /* Append bits into buffer */
    bs->bit_buffer |= (in & ((1u << n) - 1)) << bs->buffer_left;
    bs->buffer_left += n;

    /* Flush full bytes */
    while (bs->buffer_left >= 8) {
        if (bs->bytepos >= bs->length) return -1;

        bs->data[bs->bytepos++] = bs->bit_buffer & 0xFF;
        bs->bit_buffer >>= 8;
        bs->buffer_left -= 8;
    }

    return 0;
}

int bitstream_flush(struct bitStream *bs) {
    if (bs->buffer_left == 0) return 0;

    if (bs->bytepos >= bs->length) return -1;

    bs->data[bs->bytepos++] = bs->bit_buffer & 0xFF;
    bs->bit_buffer = 0;
    bs->buffer_left = 0;

    return 0;
}

int bitstream_peek(struct bitStream *bs, int n, uint32_t *out) {
    uint32_t saved_buffer = bs->bit_buffer;
    uint8_t  saved_left   = bs->buffer_left;
    size_t   saved_pos    = bs->bytepos;

    int res = bitstream_read(bs, n, out);

    bs->bit_buffer = saved_buffer;
    bs->buffer_left = saved_left;
    bs->bytepos = saved_pos;

    return res;
}

void bitstream_align_byte(struct bitStream *bs) {
    bs->bit_buffer = 0;
    bs->buffer_left = 0;
}

void bitstream_print(struct bitStream *bs) {
    printf("Bitstream (%zu bytes):\n", bs->bytepos);

    for (size_t i = 0; i < bs->bytepos; i++) {
        uint8_t byte = bs->data[i];
        printf("0x%02X  ", byte);
        for (int b = 0; b < 8; b++) {
            printf("%d", (byte >> b) & 1);
        }
        printf("\n");
    }
}

int bitstream_get_size(struct bitStream *bs) {
    return bs->bytepos + (bs->buffer_left ? 1 : 0);
}

void vflip(unsigned char *data, int width, int height, int channels) {
    if (!flip) return;

    int row_size = channels * width;
    unsigned char *tmp = malloc(channels * width);
    if (!tmp) return;

    for (int i = 0; i < height/2; i++) {
        memcpy(tmp, data + row_size * i, row_size);
        memcpy(data + row_size * i, data + row_size * (height - i - 1), row_size);
        memcpy(data + row_size * (height - i - 1), tmp, row_size);
    }

    free(tmp);
}
