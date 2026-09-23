/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* jobs.c - see jobs.h. */
#include "render/jobs.h"

#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

typedef struct {
    JobFn fn;
    void *user;
    int n;
    atomic_int next;
} JobSet;

static void *worker(void *arg)
{
    JobSet *js = arg;
    for (;;) {
        int i = atomic_fetch_add(&js->next, 1);
        if (i >= js->n) break;
        js->fn(i, js->user);
    }
    return NULL;
}

int jobs_cpus(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? 1 : n > 8 ? 8 : (int)n;
}

void jobs_run(JobFn fn, void *user, int n, int threads)
{
    if (n <= 0) return;
    if (threads <= 0) threads = jobs_cpus();
    if (threads > 8) threads = 8;
    if (threads > n) threads = n;
    JobSet js = { fn, user, n, 0 };
    atomic_init(&js.next, 0);
    pthread_t th[8];
    int started = 0;
    for (int i = 1; i < threads; i++)
        if (pthread_create(&th[started], NULL, worker, &js) == 0) started++;
    worker(&js);   /* the calling thread works too */
    for (int i = 0; i < started; i++) pthread_join(th[i], NULL);
}
