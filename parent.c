#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

void die(const char *msg) {
    write(STDERR_FILENO, msg, strlen(msg));
    exit(EXIT_FAILURE);
}

int read_line(char *buf, int max) {
    int n = 0;
    char c;
    int r;
    while ((r = read(STDIN_FILENO, &c, 1)) == 1 && c != '\n') {
        if (n < max - 1)
            buf[n++] = c;
    }
    if (r == -1)
        die("error: failed to read from stdin\n");
    buf[n] = '\0';
    return n;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    char file1[256], file2[256];
    if (read_line(file1, 256) <= 0 || read_line(file2, 256) <= 0)
        die("error: file name is empty\n");

    int p1[2], p2[2];

    if (pipe(p1) == -1 || pipe(p2) == -1)
        die("error: failed to create pipe\n");

    pid_t pid1 = fork();
    if (pid1 == -1)
        die("error: failed to fork child1\n");
    if (pid1 == 0) {
        if (dup2(p1[0], STDIN_FILENO) == -1)
            die("error: dup2 failed\n");
        close(p1[0]);
        close(p1[1]);
        close(p2[0]);
        close(p2[1]);
        execl("./child", "child", file1, NULL);
        die("error: failed to exec child\n");
    }

    pid_t pid2 = fork();
    if (pid2 == -1)
        die("error: failed to fork child2\n");
    if (pid2 == 0) {
        if (dup2(p2[0], STDIN_FILENO) == -1)
            die("error: dup2 failed\n");
        close(p1[0]);
        close(p1[1]);
        close(p2[0]);
        close(p2[1]);
        execl("./child", "child", file2, NULL);
        die("error: failed to exec child\n");
    }

    close(p1[0]);
    close(p2[0]);

    char c;
    int r;
    int line = 1;
    while ((r = read(STDIN_FILENO, &c, 1)) == 1) {
        int out = (line % 2) ? p1[1] : p2[1];
        if (write(out, &c, 1) == -1)
            die("error: failed to write to pipe\n");
        if (c == '\n')
            line++;
    }
    if (r == -1)
        die("error: failed to read from stdin\n");

    close(p1[1]);
    close(p2[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    return 0;
}
