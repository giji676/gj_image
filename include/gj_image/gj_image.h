/*
 * gj_image
 * Copyright (c) 2026 giji676
 * Licensed under MIT (see LICENSE file)
 */

#ifndef GJ_IMAGE_H
#define GJ_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Loads an image from a file.
 *
 * @param filename Path to the image file.
 * @param width    Output image width.
 * @param height   Output image height.
 * @param channels Output number of color channels.
 *
 * @return Pointer to image data (8-bit per channel RGBA).
 *         Returns NULL on failure.
 *
 * The returned buffer must be freed using gj_image_free().
 */
unsigned char *gj_image_load(const char *filename, int *width, int *height, int *channels);

/*
 * Frees the allocated image data
 *
 * @param data Pointer to the image data to be freed
 */
void           gj_image_free(unsigned char *data);

/*
 * Flip the image vertically if true.
 *
 * @param flip <1> if image should be flipped.
 */
void           gj_vflip_image(unsigned char flip);

const char*    gj_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* GJ_IMAGE_H */
