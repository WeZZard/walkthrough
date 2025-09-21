#ifndef ADA_SELECTIVE_PERSISTENCE_METRICS_H
#define ADA_SELECTIVE_PERSISTENCE_METRICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t events_processed;
    uint64_t marked_events_detected;
    uint64_t selective_dumps_performed;
    uint64_t windows_discarded;
    uint64_t avg_window_duration_ns;
    uint64_t avg_events_per_window;
    uint64_t metadata_write_failures;
} SelectivePersistenceMetrics;

void selective_persistence_metrics_reset(SelectivePersistenceMetrics* metrics);

double selective_persistence_metrics_mark_rate(const SelectivePersistenceMetrics* metrics);

double selective_persistence_metrics_dump_success_ratio(const SelectivePersistenceMetrics* metrics);

double selective_persistence_metrics_estimated_overhead(const SelectivePersistenceMetrics* metrics);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ADA_SELECTIVE_PERSISTENCE_METRICS_H
