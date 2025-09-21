#include <gtest/gtest.h>

#include <cmath>

#include <tracer_backend/selective_persistence/metrics.h>
#include <tracer_backend/selective_persistence/persistence_window.h>

TEST(SelectivePersistenceWindowTest, reset__null_pointer__then_no_crash) {
    selective_persistence_window_reset(nullptr);
}

TEST(SelectivePersistenceWindowTest, reset__populated_window__then_zeroed) {
    SelectivePersistenceWindow window{};
    window.window_id = 42;
    window.start_timestamp_ns = 100;
    window.end_timestamp_ns = 250;
    window.first_mark_timestamp_ns = 120;
    window.last_event_timestamp_ns = 200;
    window.total_events = 9;
    window.marked_events = 3;
    window.mark_seen = true;

    selective_persistence_window_reset(&window);

    EXPECT_EQ(window.window_id, 0u);
    EXPECT_EQ(window.start_timestamp_ns, 0u);
    EXPECT_EQ(window.end_timestamp_ns, 0u);
    EXPECT_EQ(window.first_mark_timestamp_ns, 0u);
    EXPECT_EQ(window.last_event_timestamp_ns, 0u);
    EXPECT_EQ(window.total_events, 0u);
    EXPECT_EQ(window.marked_events, 0u);
    EXPECT_FALSE(window.mark_seen);
}

TEST(SelectivePersistenceMetricsTest, reset__null_pointer__then_no_crash) {
    selective_persistence_metrics_reset(nullptr);
}

TEST(SelectivePersistenceMetricsTest, reset__populated_metrics__then_zeroed) {
    SelectivePersistenceMetrics metrics{};
    metrics.events_processed = 50;
    metrics.marked_events_detected = 10;
    metrics.selective_dumps_performed = 5;
    metrics.windows_discarded = 2;
    metrics.avg_window_duration_ns = 1234;
    metrics.avg_events_per_window = 20;
    metrics.metadata_write_failures = 1;

    selective_persistence_metrics_reset(&metrics);

    EXPECT_EQ(metrics.events_processed, 0u);
    EXPECT_EQ(metrics.marked_events_detected, 0u);
    EXPECT_EQ(metrics.selective_dumps_performed, 0u);
    EXPECT_EQ(metrics.windows_discarded, 0u);
    EXPECT_EQ(metrics.avg_window_duration_ns, 0u);
    EXPECT_EQ(metrics.avg_events_per_window, 0u);
    EXPECT_EQ(metrics.metadata_write_failures, 0u);
}

TEST(SelectivePersistenceMetricsTest, mark_rate__no_events__then_zero) {
    SelectivePersistenceMetrics metrics{};
    metrics.events_processed = 0;
    metrics.marked_events_detected = 5;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_mark_rate(&metrics), 0.0);
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_mark_rate(nullptr), 0.0);
}

TEST(SelectivePersistenceMetricsTest, mark_rate__with_events__then_fractional_value) {
    SelectivePersistenceMetrics metrics{};
    metrics.events_processed = 8;
    metrics.marked_events_detected = 2;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_mark_rate(&metrics), 0.25);
}

TEST(SelectivePersistenceMetricsTest, mark_rate__marks_exceed_events__then_ratio_greater_than_one) {
    SelectivePersistenceMetrics metrics{};
    metrics.events_processed = 4;
    metrics.marked_events_detected = 9;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_mark_rate(&metrics), 2.25);
}

TEST(SelectivePersistenceMetricsTest, dump_success_ratio__no_activity__then_zero) {
    SelectivePersistenceMetrics metrics{};
    metrics.selective_dumps_performed = 0;
    metrics.windows_discarded = 0;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_dump_success_ratio(&metrics), 0.0);
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_dump_success_ratio(nullptr), 0.0);
}

TEST(SelectivePersistenceMetricsTest, dump_success_ratio__with_activity__then_fractional_value) {
    SelectivePersistenceMetrics metrics{};
    metrics.selective_dumps_performed = 3;
    metrics.windows_discarded = 1;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_dump_success_ratio(&metrics), 0.75);
}

TEST(SelectivePersistenceMetricsTest, dump_success_ratio__only_discards__then_zero) {
    SelectivePersistenceMetrics metrics{};
    metrics.selective_dumps_performed = 0;
    metrics.windows_discarded = 7;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_dump_success_ratio(&metrics), 0.0);
}

TEST(SelectivePersistenceMetricsTest, estimated_overhead__no_windows__then_zero) {
    SelectivePersistenceMetrics metrics{};
    metrics.avg_events_per_window = 0;
    metrics.selective_dumps_performed = 0;
    metrics.windows_discarded = 0;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_estimated_overhead(&metrics), 0.0);
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_estimated_overhead(nullptr), 0.0);
}

TEST(SelectivePersistenceMetricsTest, estimated_overhead__avg_events_without_windows__then_zero) {
    SelectivePersistenceMetrics metrics{};
    metrics.avg_events_per_window = 15;
    metrics.selective_dumps_performed = 0;
    metrics.windows_discarded = 0;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_estimated_overhead(&metrics), 0.0);
}

TEST(SelectivePersistenceMetricsTest, estimated_overhead__clamped_to_one__then_expected) {
    SelectivePersistenceMetrics metrics{};
    metrics.avg_events_per_window = 10;
    metrics.selective_dumps_performed = 0;
    metrics.windows_discarded = 5;
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_estimated_overhead(&metrics), 1.0);
}

TEST(SelectivePersistenceMetricsTest, estimated_overhead__balanced_activity__then_fractional_value) {
    SelectivePersistenceMetrics metrics{};
    metrics.avg_events_per_window = 20;
    metrics.selective_dumps_performed = 3;
    metrics.windows_discarded = 2;
    double overhead = selective_persistence_metrics_estimated_overhead(&metrics);
    EXPECT_GT(overhead, 0.0);
    EXPECT_LT(overhead, 1.0);
}
