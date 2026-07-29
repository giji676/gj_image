#ifndef COMMON_H
#define COMMON_H

struct image_file {
    const char *filename;
    int *width;
    int *height;
    int *channels;
};

#include <stdint.h>
#include <stddef.h>

struct bitStream {
    uint8_t *data;    // pointer to the byte buffer
    size_t bytepos;   // byte position in the stream
    size_t length;    // total length of data in bytes

    uint32_t bit_buffer;
    uint8_t buffer_left;
};

void bitstream_init(struct bitStream *bs, uint8_t *data, size_t length);
int bitstream_read(struct bitStream *bs, int n, uint32_t *out);
int bitstream_peek(struct bitStream *bs, int n, uint32_t *out);
void bitstream_align_byte(struct bitStream *bs);
int bitstream_write(struct bitStream *bs, int n, uint32_t in);
int bitstream_flush(struct bitStream *bs);
void print_binary(uint32_t value, int bits);
void bitstream_print(struct bitStream *bs);
int bitstream_get_size(struct bitStream *bs);

uint32_t reverse_bits(uint32_t x, int n);

void vflip(unsigned char *data, int width, int height, int channels);

#endif // COMMON_H
