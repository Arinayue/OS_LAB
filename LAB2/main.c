#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REPEATS 5

typedef struct {
    int *a;
    size_t n;
    unsigned nthreads;
    unsigned hold;
    pthread_barrier_t bar;
} pool_t;

typedef struct {
    pool_t *pool;
    unsigned tid;
} worker_arg_t;

static void compare_range(int *a, size_t j, size_t k, size_t from, size_t to)
{
    size_t i;
    for (i = from; i < to; i++) {
        size_t l = i ^ j;
        if (l > i) {
            int t;
            if ((i & k) == 0) {
                if (a[i] > a[l]) {
                    t = a[i]; a[i] = a[l]; a[l] = t;
                }
            } else {
                if (a[i] < a[l]) {
                    t = a[i]; a[i] = a[l]; a[l] = t;
                }
            }
        }
    }
}

static void *worker(void *arg)
{
    worker_arg_t *wa = arg;
    pool_t *p = wa->pool;
    size_t chunk = p->n / p->nthreads;
    size_t rem = p->n % p->nthreads;
    size_t from = wa->tid * chunk + (wa->tid < rem ? wa->tid : rem);
    size_t to = from + chunk + (wa->tid < rem ? 1 : 0);
    size_t k, j;

    if (p->hold)
        sleep(p->hold);

    for (k = 2; k <= p->n; k <<= 1) {
        for (j = k >> 1; j > 0; j >>= 1) {
            compare_range(p->a, j, k, from, to);
            pthread_barrier_wait(&p->bar);
        }
    }
    return NULL;
}

static void run_seq(int *a, size_t n)
{
    size_t k, j;
    for (k = 2; k <= n; k <<= 1)
        for (j = k >> 1; j > 0; j >>= 1)
            compare_range(a, j, k, 0, n);
}

static void run_par(int *a, size_t n, unsigned nthreads, unsigned hold)
{
    pool_t pool;
    pthread_t *ids = malloc(nthreads * sizeof(*ids));
    worker_arg_t *args = malloc(nthreads * sizeof(*args));
    unsigned i;

    if (!ids || !args) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    pool.a = a;
    pool.n = n;
    pool.nthreads = nthreads;
    pool.hold = hold;
    if (pthread_barrier_init(&pool.bar, NULL, nthreads) != 0) {
        perror("pthread_barrier_init");
        exit(1);
    }
    for (i = 0; i < nthreads; i++) {
        args[i].pool = &pool;
        args[i].tid = i;
        if (pthread_create(&ids[i], NULL, worker, &args[i]) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }
    if (hold)
        printf("threads created, check: ps -T -p %d | wc -l\n", (int)getpid());
    fflush(stdout);
    for (i = 0; i < nthreads; i++)
        pthread_join(ids[i], NULL);

    pthread_barrier_destroy(&pool.bar);
    free(args);
    free(ids);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static size_t next_pow2(size_t x)
{
    size_t p = 1;
    while (p < x)
        p <<= 1;
    return p;
}

static int is_sorted(const int *a, size_t n)
{
    size_t i;
    for (i = 1; i < n; i++)
        if (a[i - 1] > a[i])
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    size_t n_req, n, i;
    unsigned nthreads, hold = 0;
    int *src, *buf, *ref;
    double seq_ms = 0, par_ms = 0, t0, speedup;
    int r, seq_ok = 1, par_ok = 1;
    long cores;

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: %s N THREADS [HOLD_SEC]\n", argv[0]);
        return 1;
    }
    n_req = (size_t)strtoull(argv[1], NULL, 10);
    nthreads = (unsigned)strtoul(argv[2], NULL, 10);
    if (argc == 4)
        hold = (unsigned)strtoul(argv[3], NULL, 10);
    if (n_req == 0 || nthreads == 0) {
        fprintf(stderr, "N and THREADS must be positive\n");
        return 1;
    }

    n = next_pow2(n_req);
    cores = sysconf(_SC_NPROCESSORS_ONLN);

    src = malloc(n * sizeof(int));
    buf = malloc(n * sizeof(int));
    ref = malloc(n * sizeof(int));
    if (!src || !buf || !ref) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    srand(42);
    for (i = 0; i < n_req; i++)
        src[i] = rand();
    for (; i < n; i++)
        src[i] = INT_MAX;

    for (r = 0; r < REPEATS; r++) {
        memcpy(buf, src, n * sizeof(int));
        t0 = now_ms();
        run_seq(buf, n);
        seq_ms += now_ms() - t0;
        if (!is_sorted(buf, n))
            seq_ok = 0;
    }
    seq_ms /= REPEATS;
    memcpy(ref, buf, n * sizeof(int));

    for (r = 0; r < REPEATS; r++) {
        memcpy(buf, src, n * sizeof(int));
        t0 = now_ms();
        run_par(buf, n, nthreads, r == 0 ? hold : 0);
        par_ms += now_ms() - t0;
        if (!is_sorted(buf, n) || memcmp(buf, ref, n * sizeof(int)) != 0)
            par_ok = 0;
    }
    par_ms /= REPEATS;

    speedup = seq_ms / par_ms;

    printf("pid=%d cores=%ld n=%zu (padded to %zu)\n", (int)getpid(), cores,
           n_req, n);
    printf("sorted seq=%s par=%s %s\n", seq_ok ? "OK" : "FAIL",
           par_ok ? "OK" : "FAIL", (seq_ok && par_ok) ? "OK" : "MISMATCH");
    printf("threads=%u seq_ms=%.3f par_ms=%.3f speedup=%.3f efficiency=%.3f\n",
           nthreads, seq_ms, par_ms, speedup, speedup / nthreads);

    free(ref);
    free(buf);
    free(src);
    return (seq_ok && par_ok) ? 0 : 2;
}