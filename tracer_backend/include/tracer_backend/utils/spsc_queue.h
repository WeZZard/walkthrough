#ifndef ADA_SPSC_QUEUE_H
#define ADA_SPSC_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque SPSC queue handle for uint32_t values.
// Designed for single-producer/single-consumer with lock-free semantics.
typedef struct AdaSpscQueue SPSCQueue;

// Create a queue with an internal buffer of given capacity.
// Capacity must be >= 2. It may be rounded down to the nearest power-of-two
// for performance; behavior remains correct for any capacity >= 2.
SPSCQueue* spsc_queue_create(uint32_t capacity);

// Destroy a queue created by spsc_queue_create.
void spsc_queue_destroy(SPSCQueue* q);

// Push a value onto the queue (producer thread).
// Returns true on success, false if the queue is full.
bool spsc_queue_push(SPSCQueue* q, uint32_t value);

// Pop a value from the queue (consumer thread).
// Returns true on success and stores into out_value, false if empty.
bool spsc_queue_pop(SPSCQueue* q, uint32_t* out_value);

// Introspection helpers (non-atomic snapshots; use for testing/metrics only).
bool spsc_queue_is_empty(SPSCQueue* q);
bool spsc_queue_is_full(SPSCQueue* q);
uint32_t spsc_queue_capacity(SPSCQueue* q);
uint32_t spsc_queue_size_estimate(SPSCQueue* q);

#ifdef __cplusplus
}
#endif

#endif // ADA_SPSC_QUEUE_H

