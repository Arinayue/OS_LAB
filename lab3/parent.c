#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>

#define SHM_SIZE 4096
#define NAME_LEN 64
#define LINE_MAX_LEN 256
#define WAIT_SLICE_MS 50

void die(const char *msg)
{
    write(STDERR_FILENO, msg, strlen(msg));
    exit(EXIT_FAILURE);
}

int read_line(char *buf, int max)
{
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

typedef struct {
    char shm_name[NAME_LEN];
    char sem_empty[NAME_LEN];
    char sem_full[NAME_LEN];
    int shm_fd;
    char *shm_buf;
    sem_t *empty;
    sem_t *full;
    pid_t pid;
} channel_t;

int sem_wait_retry(sem_t *s)
{
    int r;
    while ((r = sem_wait(s)) == -1 && errno == EINTR)
        ;
    return r;
}

int sem_wait_child(channel_t *ch)
{
    for (;;) {
        struct timespec ts;
        pid_t r;

        if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
            die("error: clock_gettime failed\n");
        ts.tv_nsec += WAIT_SLICE_MS * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }

        if (sem_timedwait(ch->empty, &ts) == 0)
            return 0;
        if (errno == EINTR)
            continue;
        if (errno != ETIMEDOUT)
            die("error: sem_timedwait failed\n");

        r = waitpid(ch->pid, NULL, WNOHANG);
        if (r == ch->pid || (r == -1 && errno != EINTR)) {
            ch->pid = -1;
            return (sem_trywait(ch->empty) == 0) ? 0 : -1;
        }
    }
}

void make_names(channel_t *ch, pid_t pid, int id)
{
    snprintf(ch->shm_name, NAME_LEN, "/lab3_%d_%d_shm", (int)pid, id);
    snprintf(ch->sem_empty, NAME_LEN, "/lab3_%d_%d_e", (int)pid, id);
    snprintf(ch->sem_full, NAME_LEN, "/lab3_%d_%d_f", (int)pid, id);
}

void channel_create(channel_t *ch)
{
    shm_unlink(ch->shm_name);
    sem_unlink(ch->sem_empty);
    sem_unlink(ch->sem_full);

    ch->shm_fd = shm_open(ch->shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (ch->shm_fd == -1)
        die("error: failed to create SHM\n");

    if (ftruncate(ch->shm_fd, SHM_SIZE) == -1)
        die("error: failed to resize SHM\n");

    ch->shm_buf = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                       ch->shm_fd, 0);
    if (ch->shm_buf == MAP_FAILED)
        die("error: failed to map SHM\n");

    *((uint32_t *)ch->shm_buf) = 0;

    ch->empty = sem_open(ch->sem_empty, O_RDWR | O_CREAT | O_EXCL, 0600, 1);
    if (ch->empty == SEM_FAILED)
        die("error: failed to create empty semaphore\n");

    ch->full = sem_open(ch->sem_full, O_RDWR | O_CREAT | O_EXCL, 0600, 0);
    if (ch->full == SEM_FAILED)
        die("error: failed to create full semaphore\n");
}

static channel_t *g_ch[2];

void channel_destroy(channel_t *ch);

void abort_run(const char *msg)
{
    int i;

    (void)!write(STDERR_FILENO, msg, strlen(msg));
    for (i = 0; i < 2; i++) {
        if (g_ch[i] == NULL)
            continue;
        if (g_ch[i]->pid > 0) {
            kill(g_ch[i]->pid, SIGTERM);
            waitpid(g_ch[i]->pid, NULL, 0);
            g_ch[i]->pid = -1;
        }
        channel_destroy(g_ch[i]);
    }
    exit(EXIT_FAILURE);
}

void channel_send(channel_t *ch, const char *data, uint32_t len)
{
    uint32_t *length;
    char *text;

    if (sem_wait_child(ch) == -1)
        abort_run("error: child process terminated unexpectedly\n");

    length = (uint32_t *)ch->shm_buf;
    text = ch->shm_buf + sizeof(uint32_t);
    *length = len;
    if (len != UINT32_MAX && len > 0)
        memcpy(text, data, len);

    if (sem_post(ch->full) == -1)
        die("error: sem_post full failed\n");
}

void channel_destroy(channel_t *ch)
{
    sem_close(ch->empty);
    sem_close(ch->full);
    munmap(ch->shm_buf, SHM_SIZE);
    close(ch->shm_fd);
    sem_unlink(ch->sem_empty);
    sem_unlink(ch->sem_full);
    shm_unlink(ch->shm_name);
}

pid_t spawn_child(const char *file, channel_t *ch)
{
    pid_t pid = fork();
    if (pid == -1)
        die("error: failed to fork\n");
    if (pid == 0) {
        execl("./child", "child", file, ch->shm_name, ch->sem_empty,
              ch->sem_full, (char *)NULL);
        die("error: failed to exec child\n");
    }
    ch->pid = pid;
    return pid;
}

int main(void)
{
    char file1[256], file2[256];
    char line[LINE_MAX_LEN];
    channel_t ch1, ch2;
    pid_t self;
    int n = 0;
    int lineno = 1;
    char c;
    int r;

    signal(SIGPIPE, SIG_IGN);

    if (read_line(file1, 256) <= 0 || read_line(file2, 256) <= 0)
        die("error: file name is empty\n");

    self = getpid();
    make_names(&ch1, self, 1);
    make_names(&ch2, self, 2);
    channel_create(&ch1);
    channel_create(&ch2);

    g_ch[0] = &ch1;
    g_ch[1] = &ch2;
    spawn_child(file1, &ch1);
    spawn_child(file2, &ch2);

    while ((r = read(STDIN_FILENO, &c, 1)) == 1) {
        if (c == '\n') {
            channel_send((lineno % 2) ? &ch1 : &ch2, line, (uint32_t)n);
            n = 0;
            lineno++;
        } else if (n < LINE_MAX_LEN - 1) {
            line[n++] = c;
        }
    }
    if (r == -1)
        die("error: failed to read from stdin\n");

    if (n > 0)
        channel_send((lineno % 2) ? &ch1 : &ch2, line, (uint32_t)n);

    channel_send(&ch1, NULL, UINT32_MAX);
    channel_send(&ch2, NULL, UINT32_MAX);

    if (ch1.pid > 0)
        waitpid(ch1.pid, NULL, 0);
    if (ch2.pid > 0)
        waitpid(ch2.pid, NULL, 0);

    channel_destroy(&ch1);
    channel_destroy(&ch2);
    return 0;
}
