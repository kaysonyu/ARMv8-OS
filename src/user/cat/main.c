#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char buf[512];

void cat(int fd) {
    int n;
    while (n = read(fd, buf, sizeof(buf)) > 0) {
        if (write(1, buf, n) != n) {
            fprintf(2, "cat: write error\n");
            exit(1);
        }
    }

    if (n < 0) {
        fprintf(2, "cat: read error\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    int fd, i;

    if (argc <= 1) {
        cat(0);
        exit(0);
    }

    for (i = 1; i < argc; i++) {
        fd = open(argv[i], 0);
        if (fd < 0) {
            fprintf(2, "cat: %s open error\n", argv[i]);
            exit(1);
        }

        cat(fd);
        close(fd);
    }
    exit(0);
}



