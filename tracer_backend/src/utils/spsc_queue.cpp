#include <tracer_backend/utils/spsc_queue.h>
#include <atomic>
#include <new>
#include <cstring>

struct AdaSpscQueue {
    uint32_t* buffer;
    uint32_t capacity; // number of elements
    uint32_t mask;     // if power-of-two, otherwise 0
    alignas(64) std::atomic<uint32_t> head; // consumer reads, producer writes
    alignas(64) std::atomic<uint32_t> tail; // producer writes, consumer reads
};

static inline uint32_t round_down_pow2(uint32_t v) {
    if (v < 2) return 0;
    // Round down to highest power of two <= v
    uint32_t p = 1u << (31 - __builtin_clz(v));
    return p;
}

extern "C" {

SPSCQueue* spsc_queue_create(uint32_t capacity) {
    if (capacity < 2) return nullptr;
    auto* q = new (std::nothrow) AdaSpscQueue();
    if (!q) return nullptr;
    // Round down to power-of-two to enable mask-based indexing
    uint32_t cap_p2 = round_down_pow2(capacity);
    if (cap_p2 < 2) cap_p2 = 2;
    q->capacity = cap_p2;
    q->mask = cap_p2 - 1u;
    q->buffer = new (std::nothrow) uint32_t[q->capacity];
    if (!q->buffer) { delete q; return nullptr; }
    std::memset(q->buffer, 0, sizeof(uint32_t) * q->capacity);
    q->head.store(0, std::memory_order_relaxed);
    q->tail.store(0, std::memory_order_relaxed);
    return reinterpret_cast<SPSCQueue*>(q);
}

void spsc_queue_destroy(SPSCQueue* qh) {
    if (!qh) return;
    auto* q = reinterpret_cast<AdaSpscQueue*>(qh);
    delete[] q->buffer;
    delete q;
}

bool spsc_queue_push(SPSCQueue* qh, uint32_t value) {
    if (!qh) return false;
    auto* q = reinterpret_cast<AdaSpscQueue*>(qh);
    uint32_t tail = q->tail.load(std::memory_order_acquire);
    uint32_t head = q->head.load(std::memory_order_acquire);
    uint32_t next = q->mask ? ((tail + 1) & q->mask) : ((tail + 1) % q->capacity);
    if (next == head) return false; // full
    q->buffer[tail] = value;
    q->tail.store(next, std::memory_order_release);
    return true;
}

bool spsc_queue_pop(SPSCQueue* qh, uint32_t* out_value) {
    if (!qh || !out_value) return false;
    auto* q = reinterpret_cast<AdaSpscQueue*>(qh);
    uint32_t head = q->head.load(std::memory_order_acquire);
    uint32_t tail = q->tail.load(std::memory_order_acquire);
    if (head == tail) return false; // empty
    *out_value = q->buffer[head];
    uint32_t next = q->mask ? ((head + 1) & q->mask) : ((head + 1) % q->capacity);
    q->head.store(next, std::memory_order_release);
    return true;
}

bool spsc_queue_is_empty(SPSCQueue* qh) {
    if (!qh) return true;
    auto* q = reinterpret_cast<AdaSpscQueue*>(qh);
    uint32_t head = q->head.load(std::memory_order_acquire);
    uint32_t tail = q->tail.load(std::memory_order_acquire);
    return head == tail;
}

bool spsc_queue_is_full(SPSCQueue* qh) {
    if (!qh) return false;
    auto* q = reinterpret_cast<AdaSpscQueue*>(qh);
    uint32_t tail = q->tail.load(std::memory_order_acquire);
    uint32_t head = q->head.load(std::memory_order_acquire);
    uint32_t next = q->mask ? ((tail + 1) & q->mask) : ((tail + 1) % q->capacity);
    return next == head;
}

uint32_t spsc_queue_capacity(SPSCQueue* qh) {
    if (!qh) return 0;
    auto* q = reinterpret_cast<AdaSpscQueue*>(qh);
    return q->capacity;
}

uint32_t spsc_queue_size_estimate(SPSCQueue* qh) {
    if (!qh) return 0;
    auto* q = reinterpret_cast<AdaSpscQueue*>(qh);
    uint32_t head = q->head.load(std::memory_order_acquire);
    uint32_t tail = q->tail.load(std::memory_order_acquire);
    return (tail - head) & q->mask;
}

} // extern "C"

