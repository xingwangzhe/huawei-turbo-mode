// SPDX-License-Identifier: GPL-2.0
/*
 * burn - small AVX2/FMA load generator used by verify.sh.
 *
 * It is intentionally not a benchmark of the CPU's peak: it keeps every core
 * busy with dependent FMA chains so package power and sustained clocks can be
 * compared between Huawei balanced and performance modes.
 */

#define _GNU_SOURCE
#include <immintrin.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static double g_seconds = 30.0;
static volatile int g_stop;

struct result {
	unsigned long long iterations;
	float sink;
};

static double now_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

static void *worker(void *arg)
{
	struct result *r = arg;
	__m256 a = _mm256_set1_ps(1.000001f);
	__m256 b = _mm256_set1_ps(1.000002f);
	__m256 c = _mm256_set1_ps(0.000001f);
	double start = now_seconds();
	unsigned long long n = 0;

	while (!g_stop) {
		int i;

		for (i = 0; i < 10000; i++) {
			c = _mm256_fmadd_ps(a, b, c);
			a = _mm256_fmadd_ps(b, c, a);
			b = _mm256_fmadd_ps(c, a, b);
		}
		n += 10000;
		if (now_seconds() - start >= g_seconds)
			break;
	}

	{
		float out[8];
		int i;

		_mm256_storeu_ps(out, c);
		for (i = 0; i < 8; i++)
			r->sink += out[i];
	}
	r->iterations = n;
	return NULL;
}

int main(int argc, char **argv)
{
	int threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
	struct result *results;
	pthread_t *tids;
	double start, elapsed;
	unsigned long long total = 0;
	int i;

	if (argc >= 2)
		g_seconds = atof(argv[1]);
	if (argc >= 3)
		threads = atoi(argv[2]);
	if (g_seconds <= 0 || g_seconds > 300 || threads <= 0 || threads > 512) {
		fprintf(stderr, "usage: burn [seconds 1..300] [threads]\n");
		return 2;
	}

	results = calloc(threads, sizeof(*results));
	tids = calloc(threads, sizeof(*tids));
	if (!results || !tids) {
		perror("calloc");
		return 1;
	}

	fprintf(stderr, "burn: %d threads for %.1f s\n", threads, g_seconds);
	start = now_seconds();
	for (i = 0; i < threads; i++)
		pthread_create(&tids[i], NULL, worker, &results[i]);
	for (i = 0; i < threads; i++)
		pthread_join(tids[i], NULL);
	elapsed = now_seconds() - start;

	for (i = 0; i < threads; i++)
		total += results[i].iterations;
	printf("burn: threads=%d seconds=%.2f iterations=%llu rate=%.0f it/s\n",
	       threads, elapsed, total, total / elapsed);
	return 0;
}
