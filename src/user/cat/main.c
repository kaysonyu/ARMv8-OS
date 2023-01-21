#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char buf[512];

void cat(int fd) {
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            printf("cat: write error\n");
            exit(1);
        }
    }
    if (n < 0) {
        printf("cat: read error\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        printf("cat: no arguments\n");
        exit(0);
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], 0);
        if (fd < 0) {
            printf("cat: open file %s fail\n", argv[i]);
            continue;
        }
        cat(fd);
        close(fd);
    }
    exit(0);
}