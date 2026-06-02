#include "test/display/display.h"
#include "gj_image/gj_image.h"
#include <stdio.h>

int test_image(const char *filename, int display) {
    int width, height, channels;
    unsigned char *data = gj_image_load(filename, &width, &height, &channels);
    if (!data) {
        printf("Err: %s", gj_get_last_error());
        return -1;
    }

    if (display) display_image(data, width, height, channels);

    gj_image_free(data);
    return 0;
}

int main() {
    // test_image("assets/cv.bmp");
    // real    0m0.950s
    // user    0m0.946s
    // sys     0m0.003s
    for (int i = 0; i < 1; i++) {
        test_image("/home/giji/Programming/game-2/assets/backpack/diffuse.png", 1);
    }
    for (int i = 0; i < 1; i++) {
        test_image("/home/giji/Programming/game-2/assets/backpack/specular.png", 1);
    }

    return 0;
}
