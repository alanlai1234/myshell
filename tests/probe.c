/* Deterministic test command. No shell expansion or external utilities needed. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_all(int fd, const char *s, size_t n) {
    while (n) {
        ssize_t k = write(fd, s, n);
        if (k < 0) { if (errno == EINTR) continue; return 1; }
        s += k; n -= (size_t)k;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "args")) {
        for (int i = 2; i < argc; i++) printf("[%s]\n", argv[i]);
        return fflush(stdout) == EOF;
    }
    if (!strcmp(argv[1], "status")) return argc == 3 ? atoi(argv[2]) : 2;
    if (!strcmp(argv[1], "emit")) {
        if (argc != 3) return 2;
        long n = strtol(argv[2], NULL, 10);
        if (n < 0 || n > 8388608) return 2;
        char buf[4096]; memset(buf, 'x', sizeof(buf));
        while (n) {
            size_t count = n > (long)sizeof(buf) ? sizeof(buf) : (size_t)n;
            if (write_all(1, buf, count)) return 1;
            n -= (long)count;
        }
        return 0;
    }
    if (!strcmp(argv[1], "copy")) {
        char buf[4096]; ssize_t n;
        while ((n = read(0, buf, sizeof(buf))) != 0) {
            if (n < 0) { if (errno == EINTR) continue; return 1; }
            if (write_all(1, buf, (size_t)n)) return 1;
        }
        return 0;
    }
    if (!strcmp(argv[1], "fds")) {
        int count = 0;
        for (int fd = 3; fd < 256; fd++) if (fcntl(fd, F_GETFD) != -1) count++;
        printf("%d\n", count);
        return 0;
    }
    if (!strcmp(argv[1], "out-err")) {
        return write_all(1, "OUT\n", 4) | write_all(2, "ERR\n", 4);
    }
    return 2;
}
