#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REPEATS 5

/* Аргументы одного потока на этапе битонической сортировки */
typedef struct {
	int *a;
	size_t from;
	size_t to;
	size_t j;
	size_t k;
} task_t;

/* Общий контекст пула: барьер синхронизирует этапы */
typedef struct {
	task_t task;
	size_t n;
	unsigned nthreads;
	unsigned tid;
	int done;
	pthread_barrier_t *barrier;
} worker_ctx_t;

static void swap_int(int *x, int *y)
{
	int t = *x;
	*x = *y;
	*y = t;
}

/* Один шаг compare-exchange на полуинтервале [from, to) */
static void stage_range(task_t *t)
{
	size_t i;
	for (i = t->from; i < t->to; i++) {
		size_t l = i ^ t->j;
		if (l > i) {
			/* направление зависит от бита k */
			if ((i & t->k) == 0) {
				if (t->a[i] > t->a[l])
					swap_int(&t->a[i], &t->a[l]);
			} else {
				if (t->a[i] < t->a[l])
					swap_int(&t->a[i], &t->a[l]);
			}
		}
	}
}

static void *worker(void *arg)
{
	worker_ctx_t *ctx = arg;

	for (;;) {
		pthread_barrier_wait(ctx->barrier);
		if (ctx->done)
			break;

		/* свой кусок индексов на текущем этапе */
		{
			size_t base = ctx->n / ctx->nthreads;
			size_t rem = ctx->n % ctx->nthreads;
			ctx->task.from = ctx->tid * base +
					 (ctx->tid < rem ? ctx->tid : rem);
			ctx->task.to = ctx->task.from + base +
				       (ctx->tid < rem ? 1 : 0);
			stage_range(&ctx->task);
		}

		pthread_barrier_wait(ctx->barrier);
	}
	return NULL;
}

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int is_pow2(size_t n)
{
	return n >= 2 && (n & (n - 1)) == 0;
}

static int is_sorted(const int *a, size_t n)
{
	size_t i;
	for (i = 1; i < n; i++) {
		if (a[i - 1] > a[i])
			return 0;
	}
	return 1;
}

static void fill_random(int *a, size_t n, unsigned seed)
{
	size_t i;
	srand(seed);
	for (i = 0; i < n; i++)
		a[i] = rand();
}

/* Последовательная битоническая сортировка */
static void run_seq(int *a, size_t n)
{
	size_t k, j;
	task_t t;

	t.a = a;
	for (k = 2; k <= n; k <<= 1) {
		for (j = k >> 1; j > 0; j >>= 1) {
			t.j = j;
			t.k = k;
			t.from = 0;
			t.to = n;
			stage_range(&t);
		}
	}
}

/*
 * Параллельная версия: создаём nthreads потоков один раз,
 * на каждом этапе (k, j) делим массив и ждём барьер.
 */
static void run_par(int *a, size_t n, unsigned nthreads)
{
	size_t k, j;
	unsigned i;
	pthread_t *ids;
	worker_ctx_t *ctx;
	pthread_barrier_t barrier;

	ids = malloc(nthreads * sizeof(pthread_t));
	ctx = calloc(nthreads, sizeof(worker_ctx_t));
	if (!ids || !ctx) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/* +1 — главный поток тоже участвует в барьере */
	if (pthread_barrier_init(&barrier, NULL, nthreads + 1) != 0) {
		perror("pthread_barrier_init");
		exit(1);
	}

	for (i = 0; i < nthreads; i++) {
		ctx[i].task.a = a;
		ctx[i].n = n;
		ctx[i].nthreads = nthreads;
		ctx[i].tid = i;
		ctx[i].done = 0;
		ctx[i].barrier = &barrier;
		if (pthread_create(&ids[i], NULL, worker, &ctx[i]) != 0) {
			perror("pthread_create");
			exit(1);
		}
	}

	for (k = 2; k <= n; k <<= 1) {
		for (j = k >> 1; j > 0; j >>= 1) {
			for (i = 0; i < nthreads; i++) {
				ctx[i].task.j = j;
				ctx[i].task.k = k;
			}
			pthread_barrier_wait(&barrier); /* старт этапа */
			pthread_barrier_wait(&barrier); /* конец этапа */
		}
	}

	for (i = 0; i < nthreads; i++)
		ctx[i].done = 1;
	pthread_barrier_wait(&barrier); /* потоки завершаются */

	for (i = 0; i < nthreads; i++)
		pthread_join(ids[i], NULL);

	pthread_barrier_destroy(&barrier);
	free(ctx);
	free(ids);
}

int main(int argc, char **argv)
{
	unsigned nthreads;
	size_t n;
	int *src, *seq_buf, *par_buf;
	double seq_ms = 0, par_ms = 0, t0, speedup;
	int r;
	int ok;

	if (argc != 3) {
		fprintf(stderr, "usage: %s SIZE THREADS\n", argv[0]);
		return 1;
	}

	n = (size_t)strtoul(argv[1], NULL, 10);
	nthreads = (unsigned)strtoul(argv[2], NULL, 10);
	if (!is_pow2(n)) {
		fprintf(stderr, "SIZE must be a power of two (>= 2)\n");
		return 1;
	}
	if (nthreads == 0) {
		fprintf(stderr, "THREADS must be a positive number\n");
		return 1;
	}

	src = malloc(n * sizeof(int));
	seq_buf = malloc(n * sizeof(int));
	par_buf = malloc(n * sizeof(int));
	if (!src || !seq_buf || !par_buf) {
		fprintf(stderr, "out of memory\n");
		return 1;
	}

	fill_random(src, n, 42u);

	/* усредняем время по REPEATS запускам */
	for (r = 0; r < REPEATS; r++) {
		memcpy(seq_buf, src, n * sizeof(int));
		t0 = now_ms();
		run_seq(seq_buf, n);
		seq_ms += now_ms() - t0;
	}
	seq_ms /= REPEATS;

	for (r = 0; r < REPEATS; r++) {
		memcpy(par_buf, src, n * sizeof(int));
		t0 = now_ms();
		run_par(par_buf, n, nthreads);
		par_ms += now_ms() - t0;
	}
	par_ms /= REPEATS;

	ok = is_sorted(seq_buf, n) && is_sorted(par_buf, n) &&
	     memcmp(seq_buf, par_buf, n * sizeof(int)) == 0;
	speedup = seq_ms / (par_ms > 0 ? par_ms : 1e-9);

	printf("pid=%d size=%lu\n", (int)getpid(), (unsigned long)n);
	printf("check seq/par %s\n", ok ? "OK" : "MISMATCH");
	printf("threads=%u seq_ms=%.3f par_ms=%.3f speedup=%.3f efficiency=%.3f\n",
	       nthreads, seq_ms, par_ms, speedup, speedup / nthreads);

	free(src);
	free(seq_buf);
	free(par_buf);
	return ok ? 0 : 2;
}
