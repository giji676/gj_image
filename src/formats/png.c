#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/common.h"
#include "common/adler32_simd.h"
#include "common/crc.h"
#include "error/error.h"

struct __attribute__((packed)) png_fileSignature {
    char signature[8];
};

struct __attribute__((packed)) png_chunk {
    uint32_t length;
    char chunkType[4];
    void *chunkData;
    uint32_t crc;
};

struct __attribute__((packed)) png_IHDR {
    uint32_t width;
    uint32_t height;
    uint8_t bitDepth;
    uint8_t colorType;
    uint8_t compressionMethod;
    uint8_t filterMethod;
    uint8_t interlaceMethod;
};

struct png_IDAT {
    uint8_t cmf;
    uint8_t flg;
    uint8_t cm;
    uint8_t cinfo;
    uint8_t fcheck;
    uint8_t fdict;
    uint8_t flevel;
    uint32_t data_length;
    uint8_t *data;
    uint32_t adler32;
};

// For combining all the IDAT chunks
struct png_IDAT_stream {
    uint8_t *data;
    size_t length;
};

struct png_PLTE {
    uint32_t length;
    uint8_t *data;
};

struct png_zTXt {
    char *keyword;
    uint8_t *compMethod;
    char *compText;
};

struct png_tRNS {
    uint8_t *alpha;
    uint32_t length;
};

struct png_image {
    struct png_IHDR ihdr;
    struct png_PLTE plte;
    struct png_tRNS trns;
    struct png_IDAT_stream idatStream;
    uint8_t *pixels;
    size_t totalPixelSize;
};

int png_readIDAT(void *data, uint32_t length, struct png_IDAT *idat) {
    struct bitStream bs = {0};
    bitstream_init(&bs, (uint8_t *)data, length);

    uint8_t cmf = ((uint8_t *)data)[0];
    uint8_t flg = ((uint8_t *)data)[1];

    if (((cmf << 8) | flg) % 31 != 0) {
        gj_set_error("Invalid zlib header\n");
        return -1;
    }

    uint32_t cm, cinfo;
    uint32_t fcheck, fdict, flevel;
    bitstream_read(&bs, 4, &cm);
    bitstream_read(&bs, 4, &cinfo);
    bitstream_read(&bs, 5, &fcheck);
    bitstream_read(&bs, 1, &fdict);
    bitstream_read(&bs, 2, &flevel);

    uint32_t adler32 =
        ((uint32_t)((uint8_t *)data)[length - 4] << 24) |
        ((uint32_t)((uint8_t *)data)[length - 3] << 16) |
        ((uint32_t)((uint8_t *)data)[length - 2] << 8)  |
        ((uint32_t)((uint8_t *)data)[length - 1]);

    // No need to do -2 (for the CM and CINFO bytes)
    // as we are already skipping them by starting data+2
    int data_length = length - 4; // ADLER32: 4 bytes
    if (data_length < 0) {
        gj_set_error("Invalid zlib data length\n");
        return -1;
    }
    idat->cmf = cmf;
    idat->flg = flg;
    idat->cm = cm;
    idat->cinfo = cinfo;
    idat->fcheck = fcheck;
    idat->fdict = fdict;
    idat->flevel = flevel;
    idat->data_length = data_length;
    idat->data = (uint8_t *)data+2;
    idat->adler32 = adler32;
    return 1;
}

/*
 * Buffered bit access for the DEFLATE inner loop.
 *
 * These bypass bitstream_read/bitstream_peek so a Huffman code can be peeked
 * and consumed without a call per bit. A fill that runs out of input leaves
 * the missing high bits zero, which lets the decoder peek a fixed width
 * everywhere and detect the truncation from buffer_left afterwards.
 */
static inline void png_fillBits(struct bitStream *ds, unsigned n) {
    while (ds->buffer_left < n && ds->bytepos < ds->length) {
        ds->bit_buffer |= (uint32_t)ds->data[ds->bytepos++] << ds->buffer_left;
        ds->buffer_left += 8;
    }
}

static inline uint32_t png_readBits(struct bitStream *ds, unsigned n) {
    if (n == 0) return 0;

    png_fillBits(ds, n);
    uint32_t value = ds->bit_buffer & ((1u << n) - 1);

    if (n <= ds->buffer_left) {
        ds->bit_buffer >>= n;
        ds->buffer_left -= n;
    } else {
        ds->bit_buffer = 0;
        ds->buffer_left = 0;
    }
    return value;
}

/*
 * Canonical Huffman decoding table.
 *
 * Codes of PNG_HUFF_FAST_BITS bits or fewer resolve in a single lookup:
 * fast_length/fast_symbol are indexed by the next PNG_HUFF_FAST_BITS bits of
 * the stream, so every possible bit pattern already holds its decoded symbol.
 * Longer codes are rare and fall back to a canonical search over
 * first_code/first_index/count, which costs one step per bit length rather
 * than a scan of every symbol.
 */
#define PNG_HUFF_FAST_BITS   9
#define PNG_HUFF_FAST_SIZE   (1u << PNG_HUFF_FAST_BITS)
#define PNG_HUFF_MAX_BITS    15
#define PNG_HUFF_MAX_SYMBOLS 288
#define PNG_HUFF_INVALID     0xFFFFFFFFu

struct png_huffman {
    uint8_t  fast_length[PNG_HUFF_FAST_SIZE]; // 0 when no short code matches
    uint16_t fast_symbol[PNG_HUFF_FAST_SIZE];
    uint32_t first_code[PNG_HUFF_MAX_BITS + 1];
    uint32_t first_index[PNG_HUFF_MAX_BITS + 1];
    uint32_t count[PNG_HUFF_MAX_BITS + 1];
    uint16_t sorted[PNG_HUFF_MAX_SYMBOLS];    // symbols ordered by code length
    uint32_t max_length;
};

int png_huffmanBuild(struct png_huffman *huff, const uint8_t *lengths,
                     uint32_t num_symbols) {
    if (num_symbols > PNG_HUFF_MAX_SYMBOLS) {
        gj_set_error("Too many Huffman symbols (%u)\n", num_symbols);
        return -1;
    }

    uint32_t bl_count[PNG_HUFF_MAX_BITS + 1] = {0};
    for (uint32_t i = 0; i < num_symbols; i++) {
        if (lengths[i] > PNG_HUFF_MAX_BITS) {
            gj_set_error("Huffman code length %u out of range\n", lengths[i]);
            return -1;
        }
        bl_count[lengths[i]]++;
    }
    bl_count[0] = 0;

    huff->first_code[0] = 0;
    huff->first_index[0] = 0;
    huff->count[0] = 0;
    huff->max_length = 0;

    uint32_t next_code[PNG_HUFF_MAX_BITS + 1] = {0};
    uint32_t code = 0;
    uint32_t index = 0;
    int32_t unused = 1;

    for (uint32_t len = 1; len <= PNG_HUFF_MAX_BITS; len++) {
        unused = (unused << 1) - (int32_t)bl_count[len];
        if (unused < 0) {
            gj_set_error("Over-subscribed Huffman code\n");
            return -1;
        }

        huff->first_code[len] = code;
        huff->first_index[len] = index;
        huff->count[len] = bl_count[len];
        next_code[len] = code;

        index += bl_count[len];
        code = (code + bl_count[len]) << 1;
        if (bl_count[len] > 0) huff->max_length = len;
    }

    memset(huff->fast_length, 0, sizeof(huff->fast_length));

    for (uint32_t symbol = 0; symbol < num_symbols; symbol++) {
        uint32_t len = lengths[symbol];
        if (len == 0) continue;

        uint32_t sym_code = next_code[len]++;
        huff->sorted[huff->first_index[len] + (sym_code - huff->first_code[len])] =
            (uint16_t)symbol;

        if (len > PNG_HUFF_FAST_BITS) continue;

        // The stream delivers a code most significant bit first, so the table
        // index is the code reversed. Every index sharing those low `len` bits
        // decodes to this symbol whatever the following bits turn out to be.
        uint32_t reversed = reverse_bits(sym_code, len);
        for (uint32_t i = reversed; i < PNG_HUFF_FAST_SIZE; i += 1u << len) {
            huff->fast_length[i] = (uint8_t)len;
            huff->fast_symbol[i] = (uint16_t)symbol;
        }
    }

    return 0;
}

uint32_t png_huffmanDecodeLong(struct bitStream *ds, const struct png_huffman *huff) {
    if (huff->max_length <= PNG_HUFF_FAST_BITS) return PNG_HUFF_INVALID;

    png_fillBits(ds, huff->max_length);
    uint32_t wide = ds->bit_buffer & ((1u << huff->max_length) - 1);
    uint32_t code = reverse_bits(wide, PNG_HUFF_FAST_BITS);

    for (uint32_t len = PNG_HUFF_FAST_BITS + 1; len <= huff->max_length; len++) {
        code = (code << 1) | ((wide >> (len - 1)) & 1);

        uint32_t offset = code - huff->first_code[len];
        if (offset < huff->count[len]) {
            if (len > ds->buffer_left) return PNG_HUFF_INVALID;
            ds->bit_buffer >>= len;
            ds->buffer_left -= len;
            return huff->sorted[huff->first_index[len] + offset];
        }
    }

    return PNG_HUFF_INVALID;
}

static inline uint32_t png_huffmanDecodeSymbol(struct bitStream *ds,
                                               const struct png_huffman *huff) {
    png_fillBits(ds, PNG_HUFF_FAST_BITS);
    uint32_t peeked = ds->bit_buffer & (PNG_HUFF_FAST_SIZE - 1);

    uint32_t len = huff->fast_length[peeked];
    if (len == 0) return png_huffmanDecodeLong(ds, huff);
    if (len > ds->buffer_left) return PNG_HUFF_INVALID;

    ds->bit_buffer >>= len;
    ds->buffer_left -= len;
    return huff->fast_symbol[peeked];
}

static const uint16_t dist_base[30] = {
    1,2,3,4,             // 0-3
    5,7,9,13,            // 4-7
    17,25,33,49,         // 8-11
    65,97,129,193,       // 12-15
    257,385,513,769,     // 16-19
    1025,1537,2049,3073, // 20-23
    4097,6145,8193,12289,// 24-27
    16385,24577          // 28-29
};
static const uint8_t dist_extra[30] = {
    0,0,0,0,
    1,1,2,2,
    3,3,4,4,
    5,5,6,6,
    7,7,8,8,
    9,9,10,10,
    11,11,12,12,
    13,13
};
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10,    // 257-264
    11,13,15,17,         // 265-268
    19,23,27,31,         // 269-272
    35,43,51,59,         // 273-276
    67,83,99,115,        // 277-280
    131,163,195,227,     // 281-284
    258                  // 285
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0,    // 257-264
    1,1,1,1,            // 265-268
    2,2,2,2,            // 269-272
    3,3,3,3,            // 273-276
    4,4,4,4,            // 277-280
    5,5,5,5,            // 281-284
    0                   // 285
};
int png_huffmanDecode(struct bitStream *ds,
                      uint8_t *output,
                      size_t *output_pos,
                      size_t expected,
                      const struct png_huffman *ll,
                      const struct png_huffman *dist) {
    size_t pos = *output_pos;

    while (1) {
        uint32_t symbol = png_huffmanDecodeSymbol(ds, ll);

        // Literal byte
        if (symbol < 256) {
            if (pos >= expected) {
                gj_set_error("Output buffer overflow\n");
                break;
            }
            output[pos++] = (uint8_t)symbol;
            continue;
        }

        // End of block
        if (symbol == 256) {
            *output_pos = pos;
            return 0;
        }

        if (symbol > 285) {
            gj_set_error("Unexpected symbol %u\n", symbol);
            break;
        }

        // Length/distance pair (257-285)
        uint32_t index = symbol - 257;
        uint32_t length = len_base[index] + png_readBits(ds, len_extra[index]);

        uint32_t dist_sym = png_huffmanDecodeSymbol(ds, dist);
        if (dist_sym > 29) {
            gj_set_error("Invalid distance symbol %u\n", dist_sym);
            break;
        }
        size_t distance = dist_base[dist_sym] + png_readBits(ds, dist_extra[dist_sym]);

        if (distance == 0 || distance > pos) {
            gj_set_error("Invalid distance\n");
            break;
        }
        if (length > expected - pos) {
            gj_set_error("Output buffer overflow\n");
            break;
        }

        // Must stay byte at a time: the run may overlap itself when the
        // distance is shorter than the length.
        const uint8_t *src = output + pos - distance;
        uint8_t *dst = output + pos;
        for (uint32_t i = 0; i < length; i++) {
            dst[i] = src[i];
        }
        pos += length;
    }

    *output_pos = pos;
    return -1;
}

void png_fixedCodeLengths(uint8_t *ll_lengths, uint8_t *dist_lengths) {
    uint32_t i = 0;
    for (; i < 144; i++) ll_lengths[i] = 8;
    for (; i < 256; i++) ll_lengths[i] = 9;
    for (; i < 280; i++) ll_lengths[i] = 7;
    for (; i < 288; i++) ll_lengths[i] = 8;
    for (i = 0; i < 32; i++) dist_lengths[i] = 5;
}

int png_fixedHuffmanDecode(struct bitStream *ds, uint8_t *output,
                           size_t *output_pos, size_t expected) {
    uint8_t ll_lengths[288];
    uint8_t dist_lengths[32];
    png_fixedCodeLengths(ll_lengths, dist_lengths);

    struct png_huffman ll, dist;
    if (png_huffmanBuild(&ll, ll_lengths, 288) != 0 ||
        png_huffmanBuild(&dist, dist_lengths, 32) != 0) {
        return -1;
    }

    return png_huffmanDecode(ds, output, output_pos, expected, &ll, &dist);
}

int png_dynamicHuffmanDecode(struct bitStream *ds, uint8_t *output,
                             size_t *output_pos, size_t expected) {
    static const uint8_t cl_order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
        11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    // Read headers
    uint32_t hlit, hdist, hclen;
    bitstream_read(ds, 5, &hlit); hlit += 257;
    bitstream_read(ds, 5, &hdist); hdist += 1;
    bitstream_read(ds, 4, &hclen); hclen += 4;

    // Read code-length code lengths
    uint8_t cl_lengths[19] = {0};
    for (uint32_t i = 0; i < hclen; i++) {
        uint32_t v;
        bitstream_read(ds, 3, &v);
        cl_lengths[cl_order[i]] = (uint8_t)v;
    }

    // Build code-length tree
    struct png_huffman cl;
    if (png_huffmanBuild(&cl, cl_lengths, 19) != 0) {
        return -1;
    }

    // Decode literal/length and distance code lengths
    uint8_t ll_lengths[288] = {0};
    uint8_t dist_lengths[32] = {0};
    uint32_t total_codes = hlit + hdist;
    uint32_t decoded = 0;
    uint8_t last_value = 0;

    while (decoded < total_codes) {
        uint32_t symbol = png_huffmanDecodeSymbol(ds, &cl);
        if (symbol == PNG_HUFF_INVALID) {
            gj_set_error("Invalid Huffman symbol\n");
            return -1;
        }

        if (symbol < 16) {
            uint8_t *target = (decoded < hlit) ? &ll_lengths[decoded] : &dist_lengths[decoded - hlit];
            *target = symbol;
            last_value = symbol;
            decoded++;
        } else if (symbol == 16) {
            uint32_t repeat;
            bitstream_read(ds, 2, &repeat);
            repeat += 3;
            for (uint32_t i = 0; i < repeat && decoded < total_codes; i++) {
                uint8_t *target = (decoded < hlit) ? &ll_lengths[decoded] : &dist_lengths[decoded - hlit];
                *target = last_value;
                decoded++;
            }
        } else if (symbol == 17) {
            uint32_t repeat;
            bitstream_read(ds, 3, &repeat);
            repeat += 3;
            decoded += repeat;
            last_value = 0;
        } else if (symbol == 18) {
            uint32_t repeat;
            bitstream_read(ds, 7, &repeat);
            repeat += 11;
            decoded += repeat;
            last_value = 0;
        }
    }

    // Build literal/length and distance trees
    struct png_huffman ll, dist;
    if (png_huffmanBuild(&ll, ll_lengths, hlit) != 0 ||
        png_huffmanBuild(&dist, dist_lengths, hdist) != 0) {
        return -1;
    }

    return png_huffmanDecode(ds, output, output_pos, expected, &ll, &dist);
}

int png_nonCompressed(struct bitStream *ds,
                      uint8_t *output,
                      size_t *output_pos,
                      size_t expected) {
    uint32_t len, nlen;
    bitstream_align_byte(ds);
    bitstream_read(ds, 16, &len);
    bitstream_read(ds, 16, &nlen);

    if ((len ^ 0xFFFF) != nlen) {
        gj_set_error("Stored block LEN/NLEN mismatch (LEN=%u NLEN=%u)\n", len, nlen);
        return -1;
    }

    if (*output_pos + len > expected) {
        gj_set_error("Stored block would overflow output buffer\n");
        return -1;
    }

    /* ---- Copy raw bytes ---- */
    for (uint32_t i = 0; i < len; i++) {
        uint32_t val;
        bitstream_read(ds, 8, &val);
        output[*output_pos + i] = (uint8_t)val;
    }

    *output_pos += len;
    return 0;
}

uint8_t paeth_predictor_stbi(uint8_t a, uint8_t b, uint8_t c) {
    int thresh = c*3 - (a + b);
    int lo = a < b ? a : b;
    int hi = a < b ? b : a;
    int t0 = (hi <= thresh) ? lo : c;
    int t1 = (thresh <= lo) ? hi : t0;
    return t1;
}

int png_compareAdler32(uint32_t expected, uint8_t *output, size_t output_pos) {
    uint32_t adler = 1; // initial adler state for adler32_simd_ implementation
    // TODO: Change to use 0/1 or 0/-1 instead of 1/-1
    return adler32_simd_(adler, output, output_pos) == expected ? 1 : -1;
}

uint8_t *png_processIDAT(void *data, uint32_t length,
                         struct png_IHDR *ihdr,
                         size_t *out_size) {
    int width  = ihdr->width;
    int height = ihdr->height;
    int channels;

    switch (ihdr->colorType) {
        case 2: // RGB
            if (ihdr->bitDepth != 8) {
                gj_set_error("Only 8-bit RGB supported\n");
                return NULL;
            }
            channels = 3;
            break;

        case 3: // Indexed
            if (ihdr->bitDepth != 8) {
                gj_set_error("Only 8-bit indexed supported\n");
                return NULL;
            }
            channels = 1;
            break;

        case 6: // RGBA
            if (ihdr->bitDepth != 8) {
                gj_set_error("Only 8-bit RGBA supported\n");
                return NULL;
            }
            channels = 4;
            break;

        default:
            gj_set_error("Unsupported color type %u\n", ihdr->colorType);
            return NULL;
    }

    struct png_IDAT idat;
    if (png_readIDAT(data, length, &idat) != 1) {
        return NULL;
    }

    // png_printIDAT(&idat);

    struct bitStream ds = {0};
    bitstream_init(&ds, idat.data, idat.data_length);

    size_t expected = height * (width * channels + 1);
    uint8_t *output = malloc(expected);
    if (!output) {
        gj_set_error("Failed to allocate output buffer\n");
        return NULL;
    }

    size_t output_pos = 0;

    /* ---- ZLIB / DEFLATE BLOCK LOOP ---- */
    uint32_t bfinal, btype;
    while (1) {
        bitstream_read(&ds, 1, &bfinal);
        bitstream_read(&ds, 2, &btype);

        int res;
        switch (btype) {
            case 0:
                res = png_nonCompressed(&ds, output, &output_pos, expected);
                break;
            case 1:
                res = png_fixedHuffmanDecode(&ds, output, &output_pos, expected);
                break;
            case 2:
                res = png_dynamicHuffmanDecode(&ds, output, &output_pos, expected);
                break;
            default:
                gj_set_error("Invalud BTYPE (%u)", btype);
                free(output);
                return NULL;
        }

        if (res != 0) {
            // gj_set_error("DEFLATE block decode failed\n");
            free(output);
            return NULL;
        }

        if (bfinal) break;
    }

    if (output_pos != expected) {
        gj_set_error("Size mismatch\n");
        free(output);
        return NULL;
    }

    if (png_compareAdler32(idat.adler32, output, output_pos) != 1) {
        gj_set_error("Adler32 mismatch\n");
    }

    /* ---- PNG FILTERING ---- */
    uint8_t *final_output = malloc(width * height * channels);
    if (!final_output) {
        free(output);
        return NULL;
    }

    int idx = 0;
    int row_bytes = width * channels + 1;

    for (int row = 0; row < height; row++) {
        int row_start = row * row_bytes;
        uint8_t filter = output[row_start];

        for (int i = 0; i < width * channels; i++) {
            uint8_t raw = output[row_start + 1 + i];
            uint8_t recon;

            uint8_t left = (i >= channels) ? final_output[idx - channels] : 0;
            uint8_t up   = (row > 0)  ? final_output[idx - width * channels] : 0;
            uint8_t up_left =
                (row > 0 && i >= channels) ? final_output[idx - width * channels - channels] : 0;

            switch (filter) {
                case 0: recon = raw; break;
                case 1: recon = raw + left; break;
                case 2: recon = raw + up; break;
                case 3: recon = raw + ((left + up) >> 1); break;
                case 4: 
                        recon = raw + paeth_predictor_stbi(left, up, up_left);
                        break;
                default:
                    gj_set_error("Unknown filter %u\n", filter);
                    free(output);
                    free(final_output);
                    return NULL;
            }

            final_output[idx++] = recon;
        }
    }
    free(output);

    *out_size = width * height * channels;
    return final_output;
}

void png_interpretzTXt(void *data, uint32_t length) {
    if (data == NULL || length == 0) {
        gj_set_error("Invalid zTXt data\n");
        return;
    }

    unsigned char *bytes = (unsigned char *)data;
    struct png_zTXt ztxt;
    ztxt.keyword = (char *)bytes;

    uint32_t keyword_end = 0;

    while (keyword_end < length && bytes[keyword_end] != 0) {
        keyword_end++;
    }

    if (keyword_end >= length || keyword_end + 1 >= length) {
        gj_set_error("Invalid zTXt chunk format\n");
        return;
    }
    ztxt.compMethod = &bytes[keyword_end + 1];
    ztxt.compText = (char *)&bytes[keyword_end + 2];

    // uint32_t compText_length = length - keyword_end - 2;
}

int png_compareCRC(struct png_chunk *chunk) {
    int crc_inp_len = sizeof(chunk->chunkType) + (int)chunk->length;
    unsigned char *buff = malloc(crc_inp_len);
    if (buff == NULL) {
        gj_set_error("Failed to allocate memory for chunk crc buffer\n");
        return -1;
    }
    memcpy(buff, chunk->chunkType, sizeof(chunk->chunkType));
    memcpy(buff + sizeof(chunk->chunkType), chunk->chunkData, chunk->length);

    unsigned long res = crc(buff, crc_inp_len);
    free(buff);
    if (res == chunk->crc) {
        return 1;
    } else {
        return 0;
    }
}

void png_processChunk(struct png_chunk *chunk, struct png_image *image) {
    if (strncmp(chunk->chunkType, "IHDR", 4) == 0) {
        memcpy(&image->ihdr, chunk->chunkData, sizeof(struct png_IHDR));

        image->ihdr.width  = __builtin_bswap32(image->ihdr.width);
        image->ihdr.height = __builtin_bswap32(image->ihdr.height);

        // png_printIHDR((struct png_IHDR *)chunk->chunkData);
    } else if (strncmp(chunk->chunkType, "PLTE", 4) == 0) {
        image->plte.length = chunk->length;
        image->plte.data = chunk->chunkData;
    } else if (strncmp(chunk->chunkType, "IDAT", 4) == 0) {
        size_t old_len = image->idatStream.length;
        size_t new_len = old_len + chunk->length;

        uint8_t *tmp = realloc(image->idatStream.data, new_len);
        if (!tmp) {
            gj_set_error("Failed to realloc IDAT buffer\n");
            return;
        }

        memcpy(tmp + old_len, chunk->chunkData, chunk->length);
        image->idatStream.data = tmp;
        image->idatStream.length = new_len;
    } else if (strncmp(chunk->chunkType, "zTXt", 4) == 0) {
        png_interpretzTXt(chunk->chunkData, chunk->length);
    } else if (strncmp(chunk->chunkType, "tRNS", 4) == 0) {
        image->trns.alpha = chunk->chunkData;
        image->trns.length = chunk->length;
    } else {
    }
    if (!png_compareCRC(chunk)) {
        gj_set_error("CRC NOT MATCHING\n");
    }
}

int png_readFileSignature(FILE *fptr, struct png_fileSignature *fileSignature) {
    if (fread(fileSignature, sizeof(struct png_fileSignature), 1, fptr) != 1) {
        gj_set_error("Failed to read file signature\n");
        return -1;
    }
    return 1;
}

int png_readChunk(FILE *fptr, struct png_chunk *chunk) {
    if (fread(chunk, (sizeof(chunk->length) + sizeof(chunk->chunkType)), 1,
              fptr) != 1) {
        gj_set_error("Failed to read chunk layout\n");
        return -1;
    }

    chunk->length = __builtin_bswap32(chunk->length);
    chunk->chunkData = (void *)malloc(chunk->length);
    if (chunk->chunkData == NULL) {
        gj_set_error("Failed to allocate memory for chunk data\n");
        return -1;
    }
    if (chunk->length == 0) {
        free(chunk->chunkData);
        chunk->chunkData = NULL;
    } else if (fread(chunk->chunkData, chunk->length, 1, fptr) != 1) {
        gj_set_error("Failed to read chunk data\n");
        free(chunk->chunkData);
        chunk->chunkData = NULL;
        return -1;
    }

    if (fread(&chunk->crc, sizeof(chunk->crc), 1, fptr) != 1) {
        gj_set_error("Failed to read chunk crc\n");
        free(chunk->chunkData);
        return -1;
    }
    chunk->crc = __builtin_bswap32(chunk->crc);

    return 1;
}

int png_readChunks(FILE *fptr, struct png_chunk **chunks, struct png_image *image) {
    int chunkCount = 0;

    while (1) {
        if (chunkCount > 0) {
            size_t cSize = (chunkCount + 1) * sizeof(struct png_chunk);
            struct png_chunk *temp = realloc(*chunks, cSize);
            if (temp == NULL) {
                gj_set_error("Failed to allocte memory for chunks\n");
                break;
            }
            *chunks = temp;
        }
        if (png_readChunk(fptr, &(*chunks)[chunkCount]) != 1) {
            gj_set_error("Error reading chunk or end of file\n");
            break;
        }
        png_processChunk(&(*chunks)[chunkCount], image);

        if (strncmp((*chunks)[chunkCount].chunkType, "IEND", 4) == 0) {
            // printf("\n");
            // printf("End of file reached\n");
            chunkCount++;
            break;
        }
        chunkCount++;
    }

    return chunkCount;
}

unsigned char *png_finalImageConstruction(struct png_image *image, struct image_file *image_file) {
    if (!image->pixels) {
        gj_set_error("No pixel data\n");
        return NULL;
    }
    *image_file->width  = image->ihdr.width;
    *image_file->height = image->ihdr.height;

    int has_alpha = (image->trns.length > 0) || image->ihdr.colorType == 6;
    *image_file->channels = has_alpha ? 4 : 3;

    size_t pixel_count = *image_file->width * *image_file->height;
    unsigned char *pixels = malloc(pixel_count * *image_file->channels);
    if (!pixels) {
        gj_set_error("Out of memory\n");
        return NULL;
    }

    /* truecolor (RGB) */
    if (image->ihdr.colorType == 2) {
        for (size_t i = 0; i < pixel_count; i++) {
            uint8_t r = image->pixels[i * 3 + 0];
            uint8_t g = image->pixels[i * 3 + 1];
            uint8_t b = image->pixels[i * 3 + 2];

            pixels[i * *image_file->channels + 0] = r;
            pixels[i * *image_file->channels + 1] = g;
            pixels[i * *image_file->channels + 2] = b;

            if (has_alpha) {
                pixels[i * *image_file->channels + 3] = 255;
            }
        }
        return pixels;
    }
    /* indexed color (PLTE) */
    if (image->ihdr.colorType == 3 && image->plte.length > 0) {
        for (size_t i = 0; i < pixel_count; i++) {
            uint8_t idx = image->pixels[i];
            if (!image->plte.data) {
                gj_set_error("Missing PLTE for indexed image\n");
                return NULL;
            }
            uint8_t *pal = &image->plte.data[idx * 3];

            pixels[i * *image_file->channels + 0] = pal[0];
            pixels[i * *image_file->channels + 1] = pal[1];
            pixels[i * *image_file->channels + 2] = pal[2];

            if (has_alpha) {
                if (idx < image->trns.length) {
                    // printf("idx: %u\n", idx);
                    pixels[i * *image_file->channels + 3] = image->trns.alpha[idx];
                } else {
                    pixels[i * *image_file->channels + 3] = 255;
                }
            }
        }
        return pixels;
    }

    /* truecolor (RGBA) */
    if (image->ihdr.colorType == 6) {
        for (size_t i = 0; i < pixel_count; i++) {
            uint8_t r = image->pixels[i * 4 + 0];
            uint8_t g = image->pixels[i * 4 + 1];
            uint8_t b = image->pixels[i * 4 + 2];
            uint8_t a = image->pixels[i * 4 + 3];

            pixels[i * *image_file->channels + 0] = r;
            pixels[i * *image_file->channels + 1] = g;
            pixels[i * *image_file->channels + 2] = b;
            pixels[i * *image_file->channels + 3] = a;
        }
        return pixels;
    }
    return NULL;
}

unsigned char *png_open(struct image_file *image_file) {
    FILE *fptr;

    make_crc_table();

    if ((fptr = fopen(image_file->filename, "rb")) == NULL) {
        gj_set_error("Failed to open file %s\n", image_file->filename);
        return NULL;
    }

    struct png_fileSignature png_fileSignature;
    if (png_readFileSignature(fptr, &png_fileSignature) != 1) {
        return NULL;
    }

    struct png_chunk *chunks = malloc(sizeof(struct png_chunk));
    if (chunks == NULL) {
        gj_set_error("Failed to allocte memory for chunks\n");
        return NULL;
    }

    struct png_image image = {0};
    int chunkCount = png_readChunks(fptr, &chunks, &image);
    if (chunkCount < 0) {
        gj_set_error("Error reading chunks\n");
        fclose(fptr);
        return NULL;
    }
    fclose(fptr);

    if (image.idatStream.data) {
        image.pixels = png_processIDAT(
            image.idatStream.data,
            image.idatStream.length,
            &image.ihdr,
            &image.totalPixelSize
        );

        if (!image.pixels) {
            // gj_set_error("IDAT processing failed\n");
            free(image.idatStream.data);

            for (int i = 0; i < chunkCount; ++i) {
                free(chunks[i].chunkData);
            }
            free(chunks);
            return NULL;
        }
    }

    unsigned char *data = png_finalImageConstruction(&image, image_file);
    free(image.pixels);
    free(image.idatStream.data);

    for (int i = 0; i < chunkCount; ++i) {
        free(chunks[i].chunkData);
    }
    free(chunks);

    return data;
}
