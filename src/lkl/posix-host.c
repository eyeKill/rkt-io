/*
 * Copyright 2016, 2017, 2018 Imperial College London
 */

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <sys/io.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "syscall.h"
#include "atomic.h"
#include <poll.h>
#include <lkl_host.h>
#include "lkl/iomem.h"
#include "lkl/jmp_buf.h"
#include "lkl/spdk.h"
#include <lthread.h>

/* Let's see if the host has semaphore.h */
#include <unistd.h>

#ifdef _POSIX_SEMAPHORES
#include <semaphore.h>
#define SHARE_SEM 0
#endif /* _POSIX_SEMAPHORES */

#define LKL_STDERR_FILENO 2
#define NSEC_PER_SEC 1000000000L

static void *sgxlkl_executable_alloc(size_t length) {
    void* res = mmap(0, length, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    if (res == MAP_FAILED) {
      return NULL;
    }
    return res;
}

static void *sgxlkl_executable_free(void *addr, size_t length) {
    void* res = munmap(addr, length);
    return res;
}

static void panic(void) {
    if (lthread_self()) {
        // Pin lthread so that we can make sure the host call performed by
        // fprintf completes before we crash
        lthread_self()->attr.state & BIT(LT_ST_PINNED);
    }
    fprintf(stderr, "[SGX-LKL] Kernel panic! Run with SGXLKL_KERNEL_VERBOSE=1 for more information. Aborting...\n");
    a_crash();
}

static void print(const char *str, int len) {
    write(LKL_STDERR_FILENO, str, len);
}

struct lkl_mutex {
    pthread_mutex_t mutex;
};

struct lkl_sem {
#ifdef _POSIX_SEMAPHORES
    sem_t sem;
#else
    pthread_mutex_t lock;
    int count;
    pthread_cond_t cond;
#endif /* _POSIX_SEMAPHORES */
};

struct lkl_tls_key {
        pthread_key_t key;
};

#define WARN_UNLESS(exp) do {                              \
        if (exp < 0)                                       \
            lkl_printf("%s: %s\n", #exp, strerror(errno)); \
    } while (0)

static int _warn_pthread(int ret, char *str_exp) {
    if (ret > 0)
        lkl_printf("%s: %s\n", str_exp, strerror(ret));

    return ret;
}


/* pthread_* functions use the reverse convention */
#define WARN_PTHREAD(exp) _warn_pthread(exp, #exp)

static struct lkl_sem *sem_alloc(int count) {
    struct lkl_sem *sem;

    sem = malloc(sizeof(*sem));
    if (!sem)
        return NULL;

#ifdef _POSIX_SEMAPHORES
    if (sem_init(&sem->sem, SHARE_SEM, count) < 0) {
        lkl_printf("sem_init: %s\n", strerror(errno));
        free(sem);
        return NULL;
    }
#else
    pthread_mutex_init(&sem->lock, NULL);
    sem->count = count;
    WARN_PTHREAD(pthread_cond_init(&sem->cond, NULL));
#endif /* _POSIX_SEMAPHORES */

    return sem;
}

static void sem_free(struct lkl_sem *sem) {
#ifdef _POSIX_SEMAPHORES
    WARN_UNLESS(sem_destroy(&sem->sem));
#else
    WARN_PTHREAD(pthread_cond_destroy(&sem->cond));
    WARN_PTHREAD(pthread_mutex_destroy(&sem->lock));
#endif /* _POSIX_SEMAPHORES */
    free(sem);
}

static void sem_up(struct lkl_sem *sem) {
#ifdef _POSIX_SEMAPHORES
    WARN_UNLESS(sem_post(&sem->sem));
#else
    WARN_PTHREAD(pthread_mutex_lock(&sem->lock));
    sem->count++;
    if (sem->count > 0)
        WARN_PTHREAD(pthread_cond_signal(&sem->cond));
    WARN_PTHREAD(pthread_mutex_unlock(&sem->lock));
#endif /* _POSIX_SEMAPHORES */
}

static void sem_down(struct lkl_sem *sem) {
    // Applications do not expect changes to the errno value by LKL. Keep track
    // of the current value and restore it at the end of sem_down.
    int curr_errno = errno;

#ifdef _POSIX_SEMAPHORES
    int err;

    do {
        err = sem_wait(&sem->sem);
    } while (err < 0 && errno == EINTR);
    if (err < 0 && errno != EINTR)
        lkl_printf("sem_wait: %s\n", strerror(errno));
#else
    WARN_PTHREAD(pthread_mutex_lock(&sem->lock));
    while (sem->count <= 0)
        WARN_PTHREAD(pthread_cond_wait(&sem->cond, &sem->lock));
    sem->count--;
    WARN_PTHREAD(pthread_mutex_unlock(&sem->lock));
#endif /* _POSIX_SEMAPHORES */

    // Restore errno.
    errno = curr_errno;
}

static struct lkl_mutex *mutex_alloc(int recursive) {
    struct lkl_mutex *_mutex = malloc(sizeof(struct lkl_mutex));
    pthread_mutex_t *mutex = NULL;
    pthread_mutexattr_t attr;

    if (!_mutex)
        return NULL;

    mutex = &_mutex->mutex;
    WARN_PTHREAD(pthread_mutexattr_init(&attr));

    /* PTHREAD_MUTEX_ERRORCHECK is *very* useful for debugging,
     * but has some overhead, so we provide an option to turn it
     * off. */
#ifdef DEBUG
    if (!recursive)
        WARN_PTHREAD(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
#endif /* DEBUG */

    if (recursive)
        WARN_PTHREAD(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));

    WARN_PTHREAD(pthread_mutex_init(mutex, &attr));

    return _mutex;
}

static int mutex_trylock(struct lkl_mutex *mutex) {
    return pthread_mutex_trylock(&mutex->mutex);
}

static void mutex_lock(struct lkl_mutex *mutex) {
    WARN_PTHREAD(pthread_mutex_lock(&mutex->mutex));
}

static void mutex_unlock(struct lkl_mutex *_mutex) {
    pthread_mutex_t *mutex = &_mutex->mutex;
    WARN_PTHREAD(pthread_mutex_unlock(mutex));
}

static void mutex_free(struct lkl_mutex *_mutex) {
    pthread_mutex_t *mutex = &_mutex->mutex;
    WARN_PTHREAD(pthread_mutex_destroy(mutex));
    free(_mutex);
}

static lkl_thread_t thread_create(void (*fn)(void *), void *arg) {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // default stack for linux kernel threads is 8k
    const size_t stack_size = 8 * 1024;
    pthread_attr_setstacksize(&attr, stack_size);

    if (WARN_PTHREAD(pthread_create(&thread, &attr, (void* (*)(void *))fn, arg)))
        return 0;
    else
        return (lkl_thread_t) thread;
}

static void thread_detach(void) {
    WARN_PTHREAD(pthread_detach(pthread_self()));
}

static void thread_exit(void) {
    pthread_exit(NULL);
}

static int thread_join(lkl_thread_t tid) {
    if (WARN_PTHREAD(pthread_join((pthread_t)tid, NULL)))
        return -1;
    else
        return 0;
}

static lkl_thread_t thread_self(void) {
        return (lkl_thread_t)pthread_self();
}

static int thread_equal(lkl_thread_t a, lkl_thread_t b) {
        return pthread_equal(a, b);
}

static struct lkl_tls_key *tls_alloc(void (*destructor)(void *)) {
        struct lkl_tls_key *ret = malloc(sizeof(struct lkl_tls_key));

        if (WARN_PTHREAD(pthread_key_create(&ret->key, destructor))) {
                free(ret);
                return NULL;
        }
        return ret;
}

static void tls_free(struct lkl_tls_key *key) {
        WARN_PTHREAD(lthread_key_delete(key->key));
        free(key);
}

static int tls_set(struct lkl_tls_key *key, void *data) {
        if (WARN_PTHREAD(lthread_setspecific(key->key, data)))
                return -1;
        return 0;
}

static void *tls_get(struct lkl_tls_key *key) {
        return lthread_getspecific(key->key);
}

static unsigned long long time_ns(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        panic();
    return 1e9*ts.tv_sec + ts.tv_nsec;
}

typedef struct sgx_lkl_timer {
    void (*callback_fn)();
    pthread_t thread;
    int should_stop;
} sgx_lkl_timer;

//#define LKL_DEBUG_TIMER

static void* timer_thread(void *_timer) {
    sgx_lkl_timer *timer = (sgx_lkl_timer*)_timer;
#ifdef LKL_DEBUG_TIMER
    int i = 0;
#endif
    while (!timer->should_stop) {
        usleep(20000);
#ifdef LKL_DEBUG_TIMER
        i++;
        if (i % 1000 == 0) {
            int printf(const char* f,...); printf("\033[31;1m%s() at %s:%d \033[0m\n", __func__, __FILE__, __LINE__);
        }
#endif
        timer->callback_fn();
    }
    pthread_exit(NULL);
}

static void *timer_alloc(void (*fn)(void *), void *arg) {
    sgx_lkl_timer *timer = calloc(sizeof(*timer), 1);
    if (timer == NULL) {
        fprintf(stderr, "LKL host op: timer_alloc() failed, OOM\n");
        panic();
    }

    timer->callback_fn = fn;
    return (void*)timer;
}

static int timer_start(void *_timer) {
    int res = 0;
    sgx_lkl_timer *timer = (sgx_lkl_timer*)_timer;
    timer->should_stop = 0;
    res = lthread_create(&(timer->thread), NULL, &timer_thread, (void*)timer);
    if (res != 0) {
        fprintf(stderr, "Error: pthread_create(timerfn) returned %d\n", res);
        panic();
    }
    return 0;
}

static void timer_free(void *_timer) {
    int res, exi;
    void *exit_val;
    sgx_lkl_timer *timer = (sgx_lkl_timer*)_timer;
    if (timer == NULL) {
        fprintf(stderr, "WARN: timer_free() called with NULL\n");
        panic();
    }
    timer->should_stop = 1;
    res = pthread_join(timer->thread, &exit_val);
    if (res != 0) {
        lkl_printf("WARN: pthread_join(timer) returned %d\n", res);
    }
    free(timer);
}

static long _gettid(void) {
    return (long)pthread_self();
}

struct lkl_host_operations sgxlkl_host_ops = {
    .panic = panic,
    .thread_create = thread_create,
    .thread_detach = thread_detach,
    .thread_exit = thread_exit,
    .thread_join = thread_join,
    .thread_self = thread_self,
    .thread_equal = thread_equal,
    .sem_alloc = sem_alloc,
    .sem_free = sem_free,
    .sem_up = sem_up,
    .sem_down = sem_down,
    .mutex_alloc = mutex_alloc,
    .mutex_free = mutex_free,
    .mutex_trylock = mutex_trylock,
    .mutex_lock = mutex_lock,
    .mutex_unlock = mutex_unlock,
    .tls_alloc = tls_alloc,
    .tls_free = tls_free,
    .tls_set = tls_set,
    .tls_get = tls_get,
    .time = time_ns,
    .timer_alloc = timer_alloc,
    .timer_start = timer_start,
    .timer_free = timer_free,
    .print = print,
    .mem_alloc = malloc,
    .mem_free = free,
    .spdk_malloc = sgxlkl_spdk_malloc,
    .spdk_free = sgxlkl_spdk_free,
    .mem_executable_alloc = sgxlkl_executable_alloc,
    .mem_executable_free = sgxlkl_executable_free,
    .ioremap = lkl_ioremap,
    .iomem_access = lkl_iomem_access,
    .virtio_devices = lkl_virtio_devs,
    .gettid = _gettid,
    .jmp_buf_set = sgxlkl_jmp_buf_set,
    .jmp_buf_longjmp = sgxlkl_jmp_buf_longjmp,
    .sysconf = sysconf
};

