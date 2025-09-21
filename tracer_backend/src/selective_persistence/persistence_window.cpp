#include <tracer_backend/selective_persistence/persistence_window.h>

void selective_persistence_window_reset(SelectivePersistenceWindow* window) {
    if (!window) {
        return;
    }
    window->window_id = 0;
    window->start_timestamp_ns = 0;
    window->end_timestamp_ns = 0;
    window->first_mark_timestamp_ns = 0;
    window->last_event_timestamp_ns = 0;
    window->total_events = 0;
    window->marked_events = 0;
    window->mark_seen = false;
}
