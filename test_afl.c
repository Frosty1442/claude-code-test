#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char buf[8];
    read(0, buf, 8);
    if (buf[0] == 'A' && buf[1] == 'F' && buf[2] == 'L') {
        abort(); // Intentional crash for testing
    }
    return 0;
}
