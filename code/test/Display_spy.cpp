#include "../src/PhoenixSketch/SDT.h"

int main(void) {

    printf("Press any key (q to quit):\n");

    char c;
    while (1) {
        c = getchar();
        printf("You pressed: %c\n", c);
        if (c == 'q') break;
    }

    return 0;
}