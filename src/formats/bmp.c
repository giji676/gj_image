#include <stdint.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include "common/common.h"
#include "error/error.h"

struct __attribute__((packed)) bmp_file_header {
    char signature[2];
    uint32_t size;
    uint16_t reserve1;
    uint16_t reserve2;
    uint32_t offset;
};

struct __attribute__((packed)) bmp_bitmap_info_header {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t compression;
    uint32_t sizeImage;
    int32_t horizontalRes;
    int32_t verticalRes;
    uint32_t colorsUsed;
    uint32_t colorsImportant;
};

int bmp_parse_file_header(FILE *fptr, struct bmp_file_header *header) {
    if (fread(header, sizeof(struct bmp_file_header), 1, fptr) != 1) {
        gj_set_error("Failed to parse bmp file header\n");
        return -1;
    }
    return 0;
}

int bmp_parse_bitmap_info_header(FILE *fptr, struct bmp_bitmap_info_header *header) {
    if (fread(header, sizeof(struct bmp_bitmap_info_header), 1, fptr) != 1) {
        gj_set_error("Failed to read bitmap info header\n");
        return -1;
    }
    if (header->size != 40 && header->size != 124) {
        gj_set_error("Unsuported bmp header type. (header size: %d)\n", header->size);
        return -1;
    }
    return 0;
}

unsigned char *bmp_parse_pixels(FILE *fptr,
                struct bmp_file_header *file_header,
                struct bmp_bitmap_info_header *header) {
    if (header->compression != 0) {
        gj_set_error("Compressed bmp not supported\n");
        return NULL;
    }
    int channels;
    switch (header->bitCount) {
        case 8:
            channels = 3;
            break;
        case 24:
            channels = 3; // RGB
            break;
        case 32:
            channels = 4; // RGBA
            break;
        default:
            gj_set_error("BitCount of %d not yet supported!\n", header->bitCount);
            return NULL;
    }

    int width  = header->width;
    int height = abs(header->height);
    unsigned char *pixels = malloc((size_t)width * height * channels);
    if (pixels == NULL) {
        gj_set_error("Failed to allocate memory for bmp pixel data\n");
        return NULL;
    }

    // Padded/rounded up to multiple of 4 bytes
    int paddedRowSize;

    if (header->bitCount == 8) {
        paddedRowSize = (width + 3) & ~3;
    } else {
        paddedRowSize = ((header->bitCount * width + 31) / 32) * 4;
    }

    size_t totalSize = (size_t)paddedRowSize * height;
    unsigned char *allData = malloc(totalSize);
    if (!allData) {
        gj_set_error("Failed to allocate memory for bmp read buffer\n");
        goto fail;
    }

    int actualRowSize = width * channels;

    unsigned char palette[256][4];
    int paletteSize = 0;

    if (header->bitCount == 8) {
        long paletteStart = sizeof(struct bmp_file_header) + header->size;
        long paletteEnd   = file_header->offset;
        paletteSize = (paletteEnd - paletteStart) / 4;

        if (paletteSize <= 0 || paletteSize > 256) {
            gj_set_error("Invalid bmp palette size\n");
            goto fail;
        }

        fseek(fptr, paletteStart, SEEK_SET);

        if (fread(palette, 4, paletteSize, fptr) != (size_t)paletteSize) {
            gj_set_error("Failed to read bmp palette\n");
            goto fail;
        }
    }

    fseek(fptr, file_header->offset, SEEK_SET);

    if (fread(allData, 1, totalSize, fptr) != totalSize) {
        gj_set_error("Failed to read bmp pixel data\n");
        goto fail;
    }

    for (int i = 0; i < height; ++i) {
        int dstRow = (header->height > 0)
            ? (height - 1 - i) // bottom-up
            : i;               // top-down

        unsigned char *rowData = allData + i * paddedRowSize;
        unsigned char *dst = pixels + dstRow * actualRowSize;

        if (header->bitCount == 8) {
            unsigned char *src = rowData;
            for (int j = 0; j < width; ++j) {
                unsigned char idx = *src++;
                *dst++ = palette[idx][2];
                *dst++ = palette[idx][1];
                *dst++ = palette[idx][0];
            }
        }
        // 2 branches + 2 for loops 
        // instead of 1 for loop + 2 branched 
        // for performance 

        // Convert BGR(A) to RGB(A)
        else if (channels == 3) {
            unsigned char *src = rowData;
            for (int j = 0; j < width; ++j) {
                *dst++ = src[2];
                *dst++ = src[1];
                *dst++ = src[0];
                src += 3;
            }
        }
        else if (channels == 4) {
            unsigned char *src = rowData;
            for (int j = 0; j < width; ++j) {
                *dst++ = src[2];
                *dst++ = src[1];
                *dst++ = src[0];
                *dst++ = src[3];
                src += 4;
            }
        }
    }

    free(allData);
    return pixels;

fail:
    free(allData);
    free(pixels);
    return NULL;
}

unsigned char *bmp_open(struct image_file *image_file) {
    FILE *fptr;

    if (!(fptr = fopen(image_file->filename, "rb"))) {
        gj_set_error("Failed to bmp_open file %s\n", image_file->filename);
        return NULL;
    }

    struct bmp_file_header bmp_file_header;
    if (bmp_parse_file_header(fptr, &bmp_file_header)) {
        fclose(fptr);
        return NULL;
    }

    fseek(fptr, sizeof(struct bmp_file_header), SEEK_SET);

    struct bmp_bitmap_info_header bmp_bitmap_info_header;
    if (bmp_parse_bitmap_info_header(fptr, &bmp_bitmap_info_header)) {
        fclose(fptr);
        return NULL;
    }

    fseek(fptr, bmp_file_header.offset, SEEK_SET);
    unsigned char *pixels = bmp_parse_pixels(fptr, &bmp_file_header, &bmp_bitmap_info_header);
    if (!pixels) {
        fclose(fptr);
        return NULL;
    }

    *image_file->width = bmp_bitmap_info_header.width;
    *image_file->height = abs(bmp_bitmap_info_header.height);  // Add abs()
    switch (bmp_bitmap_info_header.bitCount) {
        case 8:
            *image_file->channels = 3;
            break;
        case 24:
            *image_file->channels = 3;
            break;
        case 32:
            *image_file->channels = 4;
            break;
    }

    fclose(fptr);
    return pixels;
}
