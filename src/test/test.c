#include "test/display/display.h"
#include "gj_image/gj_image.h"
#include <stdio.h>
#include <time.h>

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
    clock_t begin = clock();

    test_image("/home/giji/Programming/gj-engine/assets/backpack/specular.png", 0);

    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    printf("%f seconds\n", time_spent);

    return 0;
}
