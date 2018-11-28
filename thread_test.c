#include "recorder.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <pthread.h>



// ****************************************************************************
//
//   Support library shared across implementations
//
// ****************************************************************************

void dawdle(unsigned minimumMs, unsigned deltaMs);


/* the default number of requests that thread need handle */
#define DEFAULT_THREAD_REQUEST_NR 4
/* the max number of requests that thread need handle */
#define MAX_THREAD_REQUEST_NR     (sizeof(uint64_t) * BITS_PER_BYTE)

#define QEMU_ALIGNED(x)      __attribute__((aligned(x)))
/* Round number down to multiple */
#define QEMU_ALIGN_DOWN(n, m) ((n) / (m) * (m))

/* Round number up to multiple. Safe when m is not a power of 2 (see
 * ROUND_UP for a faster version when a power of 2 is guaranteed) */
#define QEMU_ALIGN_UP(n, m) QEMU_ALIGN_DOWN((n) + (m) - 1, (m))

#define barrier()   ({ asm volatile("" ::: "memory"); (void)0; })
#if defined(__i386__) || defined(__x86_64__)
# define cpu_relax() asm volatile("rep; nop" ::: "memory")

#elif defined(__aarch64__)
# define cpu_relax() asm volatile("yield" ::: "memory")

#elif defined(__powerpc64__)
/* set Hardware Multi-Threading (HMT) priority to low; then back to medium */
# define cpu_relax() asm volatile("or 1, 1, 1;" \
                                  "or 2, 2, 2;" ::: "memory")

#else
# define cpu_relax() barrier()
#endif

#define atomic_fetch_add(p,v) __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST)
#define atomic_fetch_inc(ptr)   atomic_fetch_add(ptr, 1)
#define atomic_fetch_dec(ptr)   atomic_fetch_add(ptr, -1)

#define BITS_PER_BYTE           CHAR_BIT
#define BITS_PER_LONG           (sizeof (ulong) * BITS_PER_BYTE)

typedef struct QemuThread { pthread_t self; } QemuThread;
typedef struct QemuEvent {} QemuEvent;

typedef unsigned long ulong;

// ============================================================================


#if defined(FREE_LIST) || defined(BITMAP)
// ============================================================================
//
//   Support library for bitmap allocator
//
// ============================================================================

#define smp_rmb()    ({ barrier(); __atomic_thread_fence(__ATOMIC_ACQUIRE); })
#define smp_wmb()    ({ barrier(); __atomic_thread_fence(__ATOMIC_RELEASE); })
#define atomic_rcu_read(ptr)                            \
    ({                                                  \
        typeof(*ptr) _val;                              \
        __atomic_load(ptr, &_val, __ATOMIC_CONSUME);    \
        _val;                                           \
    })
#define atomic_rcu_set(ptr, i) do {                     \
        __atomic_store_n(ptr, i, __ATOMIC_RELEASE);     \
    } while(0)

#define atomic_read(ptr)                        \
    __atomic_load_n(ptr, __ATOMIC_RELAXED)

#define BIT(nr)                 (1UL << (nr))
#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)            ((nr) / BITS_PER_LONG)

#if ULONG_MAX == UINT32_MAX
# define clzl   clz32
# define ctzl   ctz32
# define clol   clo32
# define ctol   cto32
# define ctpopl ctpop32
# define revbitl revbit32
#elif ULONG_MAX == UINT64_MAX
# define clzl   clz64
# define ctzl   ctz64
# define clol   clo64
# define ctol   cto64
# define ctpopl ctpop64
# define revbitl revbit64
#else
# error Unknown sizeof long
#endif

static inline int ctz32(uint32_t val)
{
    return val ? __builtin_ctz(val) : 32;
}

static inline int ctz64(uint64_t val)
{
    return val ? __builtin_ctzll(val) : 64;
}

static inline int test_bit(long nr, const ulong *addr)
{
    return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

static inline ulong find_first_bit(const ulong *addr,
                                   ulong size)
{
    ulong result, tmp;

    for (result = 0; result < size; result += BITS_PER_LONG) {
        tmp = *addr++;
        if (tmp) {
            result += ctzl(tmp);
            return result < size ? result : size;
        }
    }
    /* Not found */
    return size;
}

ulong find_next_zero_bit(const ulong *addr, ulong size,
                         ulong offset)
{
    const ulong *p = addr + BIT_WORD(offset);
    ulong result = offset & ~(BITS_PER_LONG-1);
    ulong tmp;

    if (offset >= size) {
        return size;
    }
    size -= result;
    offset %= BITS_PER_LONG;
    if (offset) {
        tmp = *(p++);
        tmp |= ~0UL >> (BITS_PER_LONG - offset);
        if (size < BITS_PER_LONG) {
            goto found_first;
        }
        if (~tmp) {
            goto found_middle;
        }
        size -= BITS_PER_LONG;
        result += BITS_PER_LONG;
    }
    while (size & ~(BITS_PER_LONG-1)) {
        if (~(tmp = *(p++))) {
            goto found_middle;
        }
        result += BITS_PER_LONG;
        size -= BITS_PER_LONG;
    }
    if (!size) {
        return result;
    }
    tmp = *p;

found_first:
    tmp |= ~0UL << size;
    if (tmp == ~0UL) {	/* Are any bits zero? */
        return result + size;	/* Nope. */
    }
found_middle:
    return result + ctzl(~tmp);
}

#define small_nbits(nbits)                      \
    ((nbits) <= BITS_PER_LONG)

static inline void bitmap_xor(ulong *dst, const ulong *src1,
                              const ulong *src2, long nbits)
{
    if (small_nbits(nbits)) {
        *dst = *src1 ^ *src2;
    } else {

    }
}

#define atomic_xor(ptr, n) ((void) __atomic_fetch_xor(ptr, n, __ATOMIC_SEQ_CST))

static inline void change_bit_atomic(long nr, ulong *addr)
{
    ulong mask = BIT_MASK(nr);
    ulong *p = addr + BIT_WORD(nr);

    atomic_xor(p, mask);
}
// ============================================================================
#endif /* BITMAP */


#if defined(FREE_LIST) || defined(DOUBLE_LIST)
// ============================================================================
//
//   Support library for linked-list case
//
// ============================================================================

#define atomic_compare_exchange_n(Value, Expected, New)                 \
    __atomic_compare_exchange_n(&(Value), &(Expected), (New),           \
                                0, __ATOMIC_RELEASE, __ATOMIC_RELAXED)
#define atomic_read(ptr)                        \
    __atomic_load_n(ptr, __ATOMIC_RELAXED)

// ============================================================================
#endif /* DOUBLE_LIST */


// ============================================================================
//
//    Original code from proposal
//
// ============================================================================

struct ThreadedWorkqueueOps {
    /* constructor of the request */
    int (*thread_request_init)(void *request);
    /*  destructor of the request */
    void (*thread_request_uninit)(void *request);

    /* the handler of the request that is called by the thread */
    void (*thread_request_handler)(void *request);
    /* called by the user after the request has been handled */
    void (*thread_request_done)(void *request);

    size_t request_size;
};
typedef struct ThreadedWorkqueueOps ThreadedWorkqueueOps;

#define SMP_CACHE_BYTES 64

/*
 * the request representation which contains the internally used mete data,
 * it is the header of user-defined data.
 *
 * It should be aligned to the nature size of CPU.
 */
typedef struct ThreadRequest ThreadRequest;
struct ThreadRequest {
#if defined(BITMAP) || defined(FREE_LIST)
    // ------------------------------------------------------------------------
    //    Bitmap allocator
    // ------------------------------------------------------------------------
    /*
     * the request has been handled by the thread and need the user
     * to fetch result out.
     */
    uint8_t done;

    /*
     * the index to Thread::requests.
     * Save it to the padding space although it can be calculated at runtime.
     */
    uint8_t request_index;

    /* the index to Threads::per_thread_data */
    unsigned int thread_index;
    // ------------------------------------------------------------------------
#endif /* BITMAP || FREE_LIST */

#if defined(FREE_LIST) || defined(DOUBLE_LIST)
    // ------------------------------------------------------------------------
    //    Linked-list
    // ------------------------------------------------------------------------
    /*
     * Link to next request in the current list.
     * The thread request can be either in the "free" or "todo" list.
     */
    ThreadRequest *next;

    // ------------------------------------------------------------------------

#endif /* FREE_LIST || DOUBLE_LIST */
} QEMU_ALIGNED(sizeof(ulong));

struct ThreadLocal {
    struct Threads *threads;

    /* the index of the thread */
    int self;

    /* thread is useless and needs to exit */
    bool quit;

    QemuThread thread;

    void *requests;

#if defined(BITMAP) || defined(FREE_LIST)
    // ------------------------------------------------------------------------
    //   Bitmap allocator
    // ------------------------------------------------------------------------

    /*
     * the bit in these two bitmaps indicates the index of the ＠requests
     * respectively. If it's the same, the corresponding request is free
     * and owned by the user, i.e, where the user fills a request. Otherwise,
     * it is valid and owned by the thread, i.e, where the thread fetches
     * the request and write the result.
     */
    /* after the user fills the request, the bit is flipped. */
    ulong request_fill_bitmap QEMU_ALIGNED(SMP_CACHE_BYTES);
    /* after handles the request, the thread flips the bit. */
    ulong request_done_bitmap QEMU_ALIGNED(SMP_CACHE_BYTES);
    // ------------------------------------------------------------------------

#elif defined(DOUBLE_LIST)
    // ------------------------------------------------------------------------
    //   Linked list
    // ------------------------------------------------------------------------

    /*
     * A list of ThreadRequest items that the thread needs to pick up
     */
    ThreadRequest *todo QEMU_ALIGNED(SMP_CACHE_BYTES);
    // ------------------------------------------------------------------------

#endif /* DOUBLE_LIST */

    /*
     * the event used to wake up the thread whenever a valid request has
     * been submitted
     */
    QemuEvent request_valid_ev QEMU_ALIGNED(SMP_CACHE_BYTES);

    /*
     * the event is notified whenever a request has been completed
     * (i.e, become free), which is used to wake up the user
     */
    QemuEvent request_free_ev QEMU_ALIGNED(SMP_CACHE_BYTES);
};
typedef struct ThreadLocal ThreadLocal;

/*
 * the main data struct represents multithreads which is shared by
 * all threads
 */
struct Threads {
    /* the request header, ThreadRequest, is contained */
    unsigned int request_size;
    unsigned int thread_requests_nr;
    unsigned int threads_nr;
    const ThreadedWorkqueueOps *ops;

    /* the request is pushed to the thread with round-robin manner */
    unsigned int current_thread_index;

#if defined(FREE_LIST) || defined(DOUBLE_LIST)
    // ------------------------------------------------------------------------
    //   Linked-list only
    // ------------------------------------------------------------------------

    /*
     * Linked list of free requests for threads */
    ThreadRequest *free;
    // ------------------------------------------------------------------------
#endif /* FREE_LIST || DOUBLE_LIST */

    ThreadLocal per_thread_data[0] QEMU_ALIGNED(SMP_CACHE_BYTES);
};
typedef struct Threads Threads;

unsigned num_free = 0;
unsigned num_todo = 0;

RECORDER(freelist, 64, "Size of freelist and todolist");


#if defined(FREE_LIST) || defined(BITMAP)
// ============================================================================
//
//    Helpers for bitmap allocator
//
// ============================================================================

static ThreadRequest *index_to_request(ThreadLocal *thread, int request_index)
{
    ThreadRequest *request;

    request = thread->requests + request_index * thread->threads->request_size;
    assert(request->request_index == request_index);
    assert(request->thread_index == thread->self);
    return request;
}

static int request_to_index(ThreadRequest *request)
{
    return request->request_index;
}

static int request_to_thread_index(ThreadRequest *request)
{
    return request->thread_index;
}

/*
 * free request: the request is not used by any thread, however, it might
 *   contain the result need the user to call thread_request_done()
 *
 * valid request: the request contains the request data and it's committed
 *   to the thread, i,e. it's owned by thread.
 */
static uint64_t get_free_request_bitmap(Threads *threads, ThreadLocal *thread)
{
    ulong request_fill_bitmap, request_done_bitmap, result_bitmap;

    request_fill_bitmap = atomic_rcu_read(&thread->request_fill_bitmap);
    request_done_bitmap = atomic_rcu_read(&thread->request_done_bitmap);
    bitmap_xor(&result_bitmap, &request_fill_bitmap, &request_done_bitmap,
               threads->thread_requests_nr);


    /*
     * paired with smp_wmb() in mark_request_free() to make sure that we
     * read request_done_bitmap before fetching the result out.
     */
    smp_rmb();

    return result_bitmap;
}

static inline ulong find_first_zero_bit(const ulong *addr,
                                        ulong size)
{
    return find_next_zero_bit(addr, size, 0);
}

static ThreadRequest
*find_thread_free_request(Threads *threads, ThreadLocal *thread)
{
    ulong result_bitmap = get_free_request_bitmap(threads, thread);
    int index;

    index  = find_first_zero_bit(&result_bitmap, threads->thread_requests_nr);
    if (index >= threads->thread_requests_nr) {
        return NULL;
    }
    return index_to_request(thread, index);
}

static int thread_find_first_valid_request_index(ThreadLocal *thread)
{
    Threads *threads = thread->threads;
    ulong request_fill_bitmap, request_done_bitmap, result_bitmap;
    int index;

    request_fill_bitmap = atomic_rcu_read(&thread->request_fill_bitmap);
    request_done_bitmap = atomic_rcu_read(&thread->request_done_bitmap);
    bitmap_xor(&result_bitmap, &request_fill_bitmap, &request_done_bitmap,
               threads->thread_requests_nr);
    /*
     * paired with smp_wmb() in mark_request_valid() to make sure that
     * we read request_fill_bitmap before fetch the request out.
     */
    smp_rmb();

    index = find_first_bit(&result_bitmap, threads->thread_requests_nr);
    return index >= threads->thread_requests_nr ? -1 : index;
}

/*
 * the change bit operation combined with READ_ONCE and WRITE_ONCE which
 * only works on single uint64_t width
 */
static void change_bit_once(long nr, ulong *addr)
{
    uint64_t value = atomic_rcu_read(addr) ^ BIT_MASK(nr);

    atomic_rcu_set(addr, value);
}

// ============================================================================
#endif


#if defined(FREE_LIST) || defined(DOUBLE_LIST)

// ============================================================================
//
//   Helpers for linked-list allocator
//
// ============================================================================

#define atomic_push(list, item)                                 \
    do {                                                        \
        typeof(item) head = list;                               \
        do {                                                    \
            item->next = head;                                  \
        } while (!atomic_compare_exchange_n(list, head, item)); \
    } while (0)

#define atomic_pop(list)                                                \
    ({                                                                  \
      typeof(list) head;                                                \
      do {                                                              \
          head = list;                                                  \
          if (!head)                                                    \
              break;                                                    \
      } while (!atomic_compare_exchange_n(list, head, head->next));     \
      if (head)                                                         \
          head->next = NULL; /* Mark as "in flight" */                  \
      head;                                                             \
    })

static void threads_add_request(Threads *threads, ThreadRequest *request)
{
    atomic_push(threads->free, request);
    atomic_fetch_inc(&num_free);
    record(freelist, "add_request: free=%u todo=%u", num_free, num_todo);
}

// ============================================================================

#endif /* FREE_LIST || DOUBLE_LIST */



// ****************************************************************************
//
//    Main boty of thread package
//
// ****************************************************************************

RECORDER(replenish, 64, "Replenishing free list");

static ThreadRequest *find_free(Threads *threads)
{
#if defined(FREE_LIST) || defined(DOUBLE_LIST)
    ThreadRequest *request = atomic_pop(threads->free);

#elif defined(BITMAP)
    ThreadLocal *thread;
    ThreadRequest *request;
    int cur_thread, thread_index;

    cur_thread = threads->current_thread_index % threads->threads_nr;
    thread_index = cur_thread;
    do {
        thread = threads->per_thread_data + thread_index++;
        request = find_thread_free_request(threads, thread);
        if (request) {
            break;
        }
        thread_index %= threads->threads_nr;
    } while (thread_index != cur_thread);
#endif /* BITMAP */

    if (request) {
        atomic_fetch_dec(&num_free);
    }
    record(freelist, "find_free: free=%u todo=%u request=%p",
           num_free, num_todo, request);
    return request;
}

static void mark_valid(Threads *threads, ThreadRequest *request)
{
#if defined(DOUBLE_LIST)
    /* Dispatch incoming requests between threads */
    unsigned thread_index = atomic_fetch_inc(&threads->current_thread_index);
    unsigned threads_nr = threads->threads_nr;
    ThreadLocal *thread = threads->per_thread_data + thread_index % threads_nr;

    /* Atomically post request on "todo" for that thread */
    atomic_push(thread->todo, request);

#elif defined(FREE_LIST) ||  defined(BITMAP)
    int thread_index = request_to_thread_index(request);
    int request_index = request_to_index(request);
    ThreadLocal *thread = threads->per_thread_data + thread_index;

    /*
     * paired with smp_rmb() in find_first_valid_request_index() to make
     * sure the request has been filled before the bit is flipped that
     * will make the request be visible to the thread
     */
    smp_wmb();

    change_bit_once(request_index, &thread->request_fill_bitmap);
    // qemu_event_set(&thread->request_valid_ev);

#endif /* DOUBLE_LIST */

    atomic_fetch_inc(&num_todo);
    record(freelist, "mark_valid: free=%u todo=%u", num_free, num_todo);
}

static ThreadRequest *find_valid(ThreadLocal *thread)
{
    ThreadRequest *request = NULL;
#if defined(DOUBLE_LIST)
    request =  atomic_pop(thread->todo);
#elif defined(FREE_LIST) || defined(BITMAP)
    int index = thread_find_first_valid_request_index(thread);
    if (index >= 0) {
        request = index_to_request(thread, index);
    }
#endif /* DOUBLE_LIST */
    if (request) {
        atomic_fetch_dec(&num_todo);
    }
    record(freelist, "find_valid: free=%u todo=%u request=%p",
           num_free, num_todo, request);
    return request;
}

static void mark_free(ThreadLocal *thread, ThreadRequest *request)
{
#if defined(FREE_LIST) || defined(BITMAP)
    int index = request_to_index(request);

    /*
     * smp_wmb() is implied in change_bit_atomic() that is paired with
     * smp_rmb() in get_free_request_bitmap() to make sure the result
     * has been saved before the bit is flipped.
     */
    change_bit_atomic(index, &thread->request_done_bitmap);
    // qemu_event_set(&thread->request_free_ev);
#endif /* BITMAP */
#if defined(FREE_LIST) || defined(DOUBLE_LIST)
    Threads *threads = thread->threads;
    atomic_push(threads->free, request);
#endif

    atomic_fetch_dec(&num_todo);
    record(freelist, "mark_free: free=%u todo=%u, request=%p",
           num_free, num_todo, request);
}


/* retry to see if there is available request before actually go to wait. */
#define BUSY_WAIT_COUNT 1000

uintptr_t dequeue_count = 0;

static ThreadRequest *
thread_busy_wait_for_request(ThreadLocal *thread)
{
    int count = 0;

    for (count = 0; count < BUSY_WAIT_COUNT; count++) {
        atomic_fetch_inc(&dequeue_count);
        ThreadRequest *request = find_valid(thread);
        if (request) {
            return request;
        }
        cpu_relax();
    }

    return NULL;
}

RECORDER(threads, 64, "Threads that are running");
RECORDER(pauses, 64, "Pauses in thread_run");
RECORDER_TWEAK_DEFINE(server_sleep, 10, "Sleep time for server thread");
RECORDER_TWEAK_DEFINE(server_sleep_var, 0, "Sleep time variation for server");

static void *thread_run(void *opaque)
{
    ThreadLocal *self_data = (ThreadLocal *)opaque;
    Threads *threads = self_data->threads;
    void (*handler)(void *request) = threads->ops->thread_request_handler;
    ThreadRequest *request;
    int tid = self_data->self;

    record(threads, "Starting thread for %p", self_data);

    while (!atomic_read(&self_data->quit)) {
        // qemu_event_reset(&self_data->request_valid_ev);

        record(threads, "Thread %d fetching request", tid);
        request = thread_busy_wait_for_request(self_data);
        record(threads, "Thread %d got request %p", tid, request);
        if (!request) {
            dawdle(RECORDER_TWEAK(server_sleep),
                   RECORDER_TWEAK(server_sleep_var));
            // qemu_event_wait(&self_data->request_valid_ev);
            continue;
        }

#if defined(FREE_LIST) || defined(BITMAP)
        assert(!request->done);
#elif defined(DOUBLE_LIST)
        assert(!request->next);
#endif /* DOUBLE_LIST */

        record(threads, "Thread %d Handling request %p", tid, request);
        handler(request + 1);
        record(threads, "Thread %d Marking request %p as free", tid, request);
#if defined(FREE_LIST) || defined(BITMAP)
        request->done = true;
#endif /* BITMAP */
        mark_free(self_data, request);
    }

    return NULL;
}

static void uninit_thread_requests(ThreadLocal *thread, int free_nr)
{
    Threads *threads = thread->threads;
    ThreadRequest *request = thread->requests;
    void (*uninit)(void *request) = threads->ops->thread_request_uninit;
    int i;

    for (i = 0; i < free_nr; i++) {
        uninit(request + 1);
        request = (void *)request + threads->request_size;
    }
    free(thread->requests);
}

static int init_thread_requests(ThreadLocal *thread)
{
    Threads *threads = thread->threads;
    ThreadRequest *request;
    int (*init)(void *request) = threads->ops->thread_request_init;
    int ret, i, thread_reqs_size;

    thread_reqs_size = threads->thread_requests_nr * threads->request_size;
    thread_reqs_size = QEMU_ALIGN_UP(thread_reqs_size, SMP_CACHE_BYTES);
    thread->requests = calloc(1, thread_reqs_size);

    request = thread->requests;
    for (i = 0; i < threads->thread_requests_nr; i++) {
        ret = init(request + 1);
        if (ret < 0) {
            goto exit;
        }

#if defined(FREE_LIST) || defined(BITMAP)
        request->request_index = i;
        request->thread_index = thread->self;
#endif
#if defined(FREE_LIST) || defined(DOUBLE_LIST)
        threads_add_request(threads, request);
#endif /* DOUBLE_LIST */
        request = (void *)request + threads->request_size;
    }
    return 0;

exit:
    uninit_thread_requests(thread, i);
    return -1;
}

static void uninit_thread_data(Threads *threads, int free_nr)
{
    ThreadLocal *thread_local = threads->per_thread_data;
    int i;
    void *result;

    for (i = 0; i < free_nr; i++) {
        thread_local[i].quit = true;
        // qemu_event_set(&thread_local[i].request_valid_ev);
        pthread_join(thread_local[i].thread.self, &result);
        // qemu_event_destroy(&thread_local[i].request_valid_ev);
        // qemu_event_destroy(&thread_local[i].request_free_ev);
        uninit_thread_requests(&thread_local[i], threads->thread_requests_nr);
    }
}

static int
init_thread_data(Threads *threads, const char *thread_name, int thread_nr)
{
    ThreadLocal *thread_local = threads->per_thread_data;
    // char *name;
    int i;

    for (i = 0; i < thread_nr; i++) {
        thread_local[i].threads = threads;
        thread_local[i].self = i;

        if (init_thread_requests(&thread_local[i]) < 0) {
            goto exit;
        }

        // qemu_event_init(&thread_local[i].request_free_ev, false);
        // qemu_event_init(&thread_local[i].request_valid_ev, false);

        // name = g_strdup_printf("%s/%d", thread_name, thread_local[i].self);
        pthread_create(&thread_local[i].thread.self, NULL,
                       thread_run, &thread_local[i]);
        // g_free(name);
    }
    return 0;

exit:
    uninit_thread_data(threads, i);
    return -1;
}

Threads *threaded_workqueue_create(const char *name, unsigned int threads_nr,
                                   unsigned int thread_requests_nr,
                                   const ThreadedWorkqueueOps *ops)
{
    Threads *threads;

    if (threads_nr > MAX_THREAD_REQUEST_NR) {
        return NULL;
    }

    threads = calloc(1, sizeof(*threads) + threads_nr * sizeof(ThreadLocal));
    threads->ops = ops;
    threads->threads_nr = threads_nr;
    threads->thread_requests_nr = thread_requests_nr;

    threads->request_size = threads->ops->request_size;
    threads->request_size = QEMU_ALIGN_UP(threads->request_size, sizeof(long));
    threads->request_size += sizeof(ThreadRequest);

    if (init_thread_data(threads, name, threads_nr) < 0) {
        free(threads);
        return NULL;
    }

    return threads;
}

void threaded_workqueue_destroy(Threads *threads)
{
    uninit_thread_data(threads, threads->threads_nr);
    free(threads);
}

static void request_done(Threads *threads, ThreadRequest *request)
{
#if defined(FREE_LIST) || defined(BITMAP)
    if (!request->done) {
        return;
    }
    threads->ops->thread_request_done(request + 1);
    request->done = false;
#elif defined(DOUBLE_LIST)
    threads->ops->thread_request_done(request + 1);
#endif
}

void *threaded_workqueue_get_request(Threads *threads)
{
    ThreadRequest *request = find_free(threads);
    if (!request) {
        return NULL;
    }

    request_done(threads, request);
    return request + 1;
}

void threaded_workqueue_submit_request(Threads *threads, void *request)
{
    ThreadRequest *req = (ThreadRequest *) request - 1;
#if defined(FREE_LIST) || defined(BITMAP)
    int thread_index = request_to_thread_index(request);

    assert(!req->done);
    mark_valid(threads, req);
    threads->current_thread_index = thread_index  + 1;
#elif defined(DOUBLE_LIST) /* !DOUBLE_LIST */
    mark_valid(threads, req);
#endif /* DOUBLE_LIST */
}

void threaded_workqueue_wait_for_requests(Threads *threads)
{
#if defined(FREE_LIST) || defined(BITMAP)
    ThreadLocal *thread;
    ulong result_bitmap;
    int thread_index, index = 0;

    for (thread_index = 0; thread_index < threads->threads_nr; thread_index++) {
        thread = threads->per_thread_data + thread_index;
        index = 0;
    retry:
        // qemu_event_reset(&thread->request_free_ev);
        result_bitmap = get_free_request_bitmap(threads, thread);

        for (; index < threads->thread_requests_nr; index++) {
            if (test_bit(index, &result_bitmap)) {
                // qemu_event_wait(&thread->request_free_ev);
                goto retry;
            }

            request_done(threads, index_to_request(thread, index));
        }
    }
#elif defined(DOUBLE_LIST)
    ThreadLocal *thread;
    int thread_index, index = 0;

    for (thread_index = 0; thread_index < threads->threads_nr; thread_index++) {
        thread = threads->per_thread_data + thread_index;
        index = 0;
        // qemu_event_reset(&thread->request_free_ev);
        while (atomic_read(&thread->todo) != NULL)
            /* spin */;
    }
#endif /* DOUBLE_LIST */
}

RECORDER(client, 128, "Record client operations");

typedef struct CompressData { unsigned block, offset; } CompressData;

static unsigned inits = 0;
static unsigned finis = 0;
static unsigned handlers = 0;
static unsigned dones = 0;
static unsigned running = 0;

static int compress_thread_data_init(void *request)
{
    record(client, "Init request %p", request);
    atomic_fetch_inc(&inits);
    return 0;
}

static void compress_thread_data_fini(void *request)
{
    record(client, "Fini request %p", request);
    atomic_fetch_inc(&finis);
}

RECORDER(running, 128, "Number of running items");
RECORDER_TWEAK_DEFINE(workload_min, 0, "Minimum duration of workload");
RECORDER_TWEAK_DEFINE(workload_var, 0, "Variation in duration of workload");

static void compress_thread_data_handler(void *request)
{
    unsigned run = atomic_fetch_inc(&running);
    CompressData *cd = request;
    record(running, "Handler request %p running=%u", cd, run);
    atomic_fetch_inc(&handlers);
    dawdle(RECORDER_TWEAK(workload_min), RECORDER_TWEAK(workload_var));
    atomic_fetch_dec(&running);
}

static void compress_thread_data_done(void *request)
{
    CompressData *cd = request;
    record(client, "Done request %p delta=%d", cd, dones - handlers);
    atomic_fetch_inc(&dones);
}

static const ThreadedWorkqueueOps compress_ops = {
    .thread_request_init = compress_thread_data_init,
    .thread_request_uninit = compress_thread_data_fini,
    .thread_request_handler = compress_thread_data_handler,
    .thread_request_done = compress_thread_data_done,
    .request_size = sizeof(CompressData),
};

static Threads *compress_threads;

static void flush_compressed_data()
{
    threaded_workqueue_wait_for_requests(compress_threads);
}

static void compress_threads_save_cleanup(void)
{
    if (!compress_threads) {
        return;
    }

    threaded_workqueue_destroy(compress_threads);
    compress_threads = NULL;
}

RECORDER_TWEAK_DEFINE(compress_threads, 32, "Number of server threads");
RECORDER_TWEAK_DEFINE(compress_tasks, 4, "Number of server threads");

static int compress_threads_save_setup(void)
{
    unsigned num_threads = RECORDER_TWEAK(compress_threads);
    unsigned num_tasks = RECORDER_TWEAK(compress_tasks);
    compress_threads = threaded_workqueue_create("compress",
                                                 num_threads,
                                                 num_tasks,
                                                 &compress_ops);
    return compress_threads ? 0 : -1;
}

RECORDER(client_loop, 128, "Client loops");
RECORDER(failures, 64, "Failures to get a request");
RECORDER_TWEAK_DEFINE(client_wait, 0, "Client wait time (min)");
RECORDER_TWEAK_DEFINE(client_wait_var, 10, "Client wait time (variation)");

unsigned success_reqs = 0;
unsigned failed_reqs = 0;

static int compress_page_with_multi_thread(unsigned block, unsigned offset)
{
    CompressData *cd;
    bool wait = RECORDER_TWEAK(client_wait) != 0;

retry:
    cd = threaded_workqueue_get_request(compress_threads);
    record(client_loop, "Got request %p", cd);
    if (!cd) {
        /*
         * wait for the free thread if the user specifies
         * 'compress-wait-thread', otherwise we will post
         *  the page out in the main thread as normal page.
         */
        record(client, "Failed, will %+s", wait ? "retry" : "not retry");
        atomic_fetch_inc(&failed_reqs);
        if (wait) {
            dawdle(RECORDER_TWEAK(client_wait),
                   RECORDER_TWEAK(client_wait_var));
            goto retry;
        }

        return -1;
    }
    cd->block = block;
    cd->offset = offset;
    threaded_workqueue_submit_request(compress_threads, cd);
    atomic_fetch_inc(&success_reqs);
    return 1;
}


// ****************************************************************************
//  recorder_test.c                                           Recorder project
// ****************************************************************************
//
//   File Description:
//
//     Test for the flight recorder
//
//     This tests that we can record things and dump them.
//
//
//
//
//
//
// ****************************************************************************
//  (C) 2017-2018 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See file LICENSE for details.
// ****************************************************************************

#include "recorder_ring.h"
#include "recorder.h"
#include "alt_drand48.h"

#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


int failed = 0;

RECORDER(MAIN,          64, "Global operations in 'main()'");
RECORDER(Pauses,        256, "Pauses during blocking operations");
RECORDER(Special,        64, "Special operations to the recorder");
RECORDER(SpeedTest,      32, "Recorder speed test");
RECORDER(SpeedInfo,      32, "Recorder information during speed test");
RECORDER(FastSpeedTest,  32, "Fast recorder speed test");



// ============================================================================
//
//    Flight recorder testing
//
// ============================================================================

uintptr_t recorder_count = 0;
unsigned pauses_count = 0;

#define INFO(...)                                       \
    do                                                  \
    {                                                   \
        record(MAIN, __VA_ARGS__);                      \
        char buf[256];                                  \
        buf[0] = '#'; buf[1] = ' ';                     \
        snprintf(buf+2, sizeof(buf)-2, __VA_ARGS__);    \
        puts(buf);                                      \
    } while(0)

#define VERBOSE(...) if (debug) INFO(__VA_ARGS__)

#define FAIL(...)                                       \
    do                                                  \
    {                                                   \
        record(MAIN, "FAILURE");                        \
        record(MAIN, __VA_ARGS__);                      \
        char buf[256];                                  \
        snprintf(buf, sizeof(buf), __VA_ARGS__);        \
        puts(buf);                                      \
        failed = 1;                                     \
    } while(0)

unsigned thread_id = 0;
unsigned threads_to_stop = 0;

void dawdle(unsigned minimumMs, unsigned deltaMs)
{
    struct timespec tm;
    tm.tv_sec  = 0;
    tm.tv_nsec = (minimumMs + drand48() * deltaMs) * 1000000;
    record(Pauses, "Pausing #%u %ld.%03dus",
           recorder_ring_fetch_add(pauses_count, 1),
           tm.tv_nsec / 1000, tm.tv_nsec % 1000);
    nanosleep(&tm, NULL);
}

// RECORDER(SpeedTest,      32, "Recorder speed test");
RECORDER_TWEAK_DEFINE(sleep_time, 0, "Sleep time between records");
RECORDER_TWEAK_DEFINE(sleep_time_delta, 0, "Variations in sleep time between records");

void *recorder_thread(void *thread)
{
    uintptr_t i = 0;
    unsigned tid = (unsigned) (uintptr_t) thread;

    while (!threads_to_stop)
    {
        i++;
        compress_page_with_multi_thread(i % 512 + tid, i % 387);
    }
    recorder_ring_fetch_add(recorder_count, i);
    recorder_ring_fetch_add(threads_to_stop, -1);
    return NULL;
}

void *recorder_fast_thread(void *thread)
{
    uintptr_t i = 0;
    unsigned tid = (unsigned) (uintptr_t) thread;
    while (!threads_to_stop)
    {
        i++;
        RECORD_FAST(FastSpeedTest, "[thread %u] Fast recording %u mod %u",
                    tid, i, i % 700);
    }
    recorder_ring_fetch_add(recorder_count, i);
    recorder_ring_fetch_add(threads_to_stop, -1);
    return NULL;
}

typedef struct example { int x; int y; int z; } example_t;

size_t show_struct(intptr_t trace,
                   const char *format, char *buffer, size_t len, uintptr_t data)
{
    example_t *e = (example_t *) data;
    size_t s = trace
        ? snprintf(buffer, len, "example(%d, %d, %d)", e->x, e->y, e->z)
        : snprintf(buffer, len, "example(%p)", e);
    return s;
}

RECORDER_TWEAK_DEFINE(run_duration, 1, "Default run duration");
RECORDER_TWEAK_DEFINE(client_threads, 1, "Default number of client threads");

#if defined(BITMAP)
#define NAME "bit map"
#elif defined(FREE_LIST)
#define NAME "free list"
#elif defined(DOUBLE_LIST)
#define NAME "double list"
#endif /* DOUBLE_LIST */

void allocator_test(int argc, char **argv)
{
    int i, j;
    uintptr_t count = argc >= 2 ? atoi(argv[1]):RECORDER_TWEAK(client_threads);
    if (argc >= 3)
        RECORDER_TWEAK(compress_threads) = atoi(argv[2]);
    unsigned howLong = argc >= 4 ? atoi(argv[3]) : RECORDER_TWEAK(run_duration);
    if (argc >= 5)
        RECORDER_TWEAK(compress_tasks) = atoi(argv[4]);

    INFO("Testing " NAME " allocation");
    if (RECORDER_CURRENT_VERSION > RECORDER_VERSION(1,3))
        FAIL("Testing an unexpected version of the recorder, "
             "update RECORDER_CURRENT_VERSION");

    for (i = 0; i < 1; i++)
    {
        recorder_count = 0;

        INFO("Launching: %lu served by %lu for %u seconds",
             count, RECORDER_TWEAK(compress_threads), howLong);
        record(MAIN, "Starting %s speed test for %us with %u threads",
               i ? "fast" : "normal", howLong, count);
        compress_threads_save_setup();

        pthread_t tid;
        for (j = 0; j < count; j++)
            pthread_create(&tid, NULL,
                           i ? recorder_fast_thread : recorder_thread,
                           (void *) (intptr_t) j);

        INFO("Allocator testing for "NAME" in progress, please wait about %ds",
             howLong);
        unsigned sleepTime = howLong;
        do { sleepTime =  sleep(sleepTime); } while (sleepTime);
        INFO("Allocator testing for "NAME" completed, stopping threads");
        threads_to_stop = count;

        while(threads_to_stop)
        {
            record(Pauses, "Waiting for threads to stop, %u remaining",
                   threads_to_stop);
            flush_compressed_data();
            dawdle(1, 0);
        }
        INFO("All threads have stopped, %lu iterations",
             recorder_count);

        compress_threads_save_cleanup();
        recorder_count += (recorder_count == 0);
        dequeue_count += (recorder_count == 0);

        printf("# Test analysis ("NAME" allocator):\n"
               "CLIENT_ITERATIONS=%lu\n"
               "CLIENT_ITERATIONS_PER_MS=%lu\n"
               "ENQUEUE_DURATION_NS=%u\n"
               "THREAD_ITERATIONS=%lu\n"
               "THREAD_ITERATIONS_PER_MS=%lu\n"
               "DEQUEUE_DURATION_NS=%u\n"
               "CLIENT_THREADS=%lu\n"
               "SERVER_THREADS=%lu\n"
               "SUCCESSFUL_REQUESTS=%u\n"
               "FAILED_REQUESTS=%u\n"
               "HANDLERS=%u\n"
               "DONES=%u\n"
               "INIT=%u\n"
               "FINI=%u\n"
               ,
               recorder_count,
               recorder_count / (howLong * 1000),
               (unsigned) (howLong * 1000000000ULL / recorder_count),
               dequeue_count,
               dequeue_count / (howLong * 1000),
               (unsigned) (howLong * 1000000000ULL / dequeue_count),
               count, RECORDER_TWEAK(compress_threads),
               success_reqs, failed_reqs,
               handlers, dones, inits, finis);

    }
}




// ============================================================================
//
//    Main entry point
//
// ============================================================================

        int main(int argc, char **argv)
    {
        recorder_dump_on_common_signals(0, 0);
        allocator_test(argc, argv);
        return failed;
    }
