#include <tracer_backend/selective_persistence/metrics.h>

#include <algorithm>

namespace {

double safe_divide(double numerator, double denominator) {
    if (denominator == 0.0) {
        return 0.0;
    }
    return numerator / denominator;
}

}  // namespace

void selective_persistence_metrics_reset(SelectivePersistenceMetrics* metrics) {
    if (!metrics) {
        return;
    }
    metrics->events_processed = 0;
    metrics->marked_events_detected = 0;
    metrics->selective_dumps_performed = 0;
    metrics->windows_discarded = 0;
    metrics->avg_window_duration_ns = 0;
    metrics->avg_events_per_window = 0;
    metrics->metadata_write_failures = 0;
}

double selective_persistence_metrics_mark_rate(const SelectivePersistenceMetrics* metrics) {
    if (!metrics || metrics->events_processed == 0) {
        return 0.0;
    }
    return safe_divide(static_cast<double>(metrics->marked_events_detected),
                       static_cast<double>(metrics->events_processed));
}

double selective_persistence_metrics_dump_success_ratio(const SelectivePersistenceMetrics* metrics) {
    if (!metrics) {
        return 0.0;
    }
    double dumps = static_cast<double>(metrics->selective_dumps_performed);
    double discards = static_cast<double>(metrics->windows_discarded);
    return safe_divide(dumps, dumps + discards);
}

double selective_persistence_metrics_estimated_overhead(const SelectivePersistenceMetrics* metrics) {
    if (!metrics) {
        return 0.0;
    }
    double avg_events = static_cast<double>(metrics->avg_events_per_window);
    double total_windows = static_cast<double>(metrics->selective_dumps_performed +
                                               metrics->windows_discarded);
    if (avg_events == 0.0 || total_windows == 0.0) {
        return 0.0;
    }
    double wasted_events = static_cast<double>(metrics->windows_discarded) * avg_events;
    double total_events = total_windows * avg_events;
    double overhead = safe_divide(wasted_events, total_events);
    return std::clamp(overhead, 0.0, 1.0);
}
