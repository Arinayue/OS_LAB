#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

void die(const char *msg) {
    write(STDERR_FILENO, msg, strlen(msg));
    exit(EXIT_FAILURE);
}

void write_rev(char *s, int n) {
    for (int i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
    s[n] = '\n';
    if (write(STDOUT_FILENO, s, n + 1) == -1)
        die("error: failed to write to file\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2)
        die("error: usage: child filename\n");

    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
        die("error: failed to open file\n");
    if (dup2(fd, STDOUT_FILENO) == -1)
        die("error: dup2 failed\n");
    close(fd);

    char buf[256];
    int n = 0;
    char c;
    int r;
    while ((r = read(STDIN_FILENO, &c, 1)) == 1) {
        if (c == '\n') {
            write_rev(buf, n);
            n = 0;
        } else if (n < 255) {
            buf[n++] = c;
        }
    }
    if (r == -1)
        die("error: failed to read from stdin\n");
    if (n > 0) /* последняя строка без \n */
        write_rev(buf, n);
    return 0;
}
