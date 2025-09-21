#ifndef ADA_SELECTIVE_PERSISTENCE_PERSISTENCE_WINDOW_H
#define ADA_SELECTIVE_PERSISTENCE_PERSISTENCE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SelectivePersistenceWindow {
    uint64_t window_id;
    uint64_t start_timestamp_ns;
    uint64_t end_timestamp_ns;
    uint64_t first_mark_timestamp_ns;
    uint64_t last_event_timestamp_ns;
    uint64_t total_events;
    uint64_t marked_events;
    bool mark_seen;
} SelectivePersistenceWindow;

void selective_persistence_window_reset(SelectivePersistenceWindow* window);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ADA_SELECTIVE_PERSISTENCE_PERSISTENCE_WINDOW_H
