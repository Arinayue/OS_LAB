#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <semaphore.h>

#define SHM_SIZE 4096

void die(const char *msg)
{
    write(STDERR_FILENO, msg, strlen(msg));
    exit(EXIT_FAILURE);
}

int sem_wait_retry(sem_t *s)
{
    int r;
    while ((r = sem_wait(s)) == -1 && errno == EINTR)
        ;
    return r;
}

void write_rev(int fd, char *s, int n)
{
    int i;
    for (i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
    s[n] = '\n';
    if (write(fd, s, n + 1) == -1)
        die("error: failed to write to file\n");
}

int main(int argc, char *argv[])
{
    int out_fd, shm_fd, running;
    char *shm_buf;
    sem_t *empty, *full;

    if (argc < 5)
        die("error: usage: child filename shm_name sem_empty sem_full\n");

    out_fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1)
        die("error: failed to open file\n");

    shm_fd = shm_open(argv[2], O_RDWR, 0);
    if (shm_fd == -1)
        die("error: failed to open SHM\n");

    shm_buf = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   shm_fd, 0);
    if (shm_buf == MAP_FAILED)
        die("error: failed to map SHM\n");

    empty = sem_open(argv[3], O_RDWR);
    if (empty == SEM_FAILED)
        die("error: failed to open empty semaphore\n");

    full = sem_open(argv[4], O_RDWR);
    if (full == SEM_FAILED)
        die("error: failed to open full semaphore\n");

    running = 1;
    while (running) {
        uint32_t len;
        char buf[256];

        if (sem_wait_retry(full) == -1)
            die("error: sem_wait full failed\n");

        len = *((uint32_t *)shm_buf);

        if (len == UINT32_MAX) {
            running = 0;
        } else {
            if (len > 255)
                len = 255;
            memcpy(buf, shm_buf + sizeof(uint32_t), len);
            write_rev(out_fd, buf, (int)len);
        }

        if (sem_post(empty) == -1)
            die("error: sem_post empty failed\n");
    }

    sem_close(empty);
    sem_close(full);
    munmap(shm_buf, SHM_SIZE);
    close(shm_fd);
    close(out_fd);
    return 0;
}
