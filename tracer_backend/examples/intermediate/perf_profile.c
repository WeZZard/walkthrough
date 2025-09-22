/*
 * perf_profile.c - Algorithm profiling ADA tracing example
 *
 * Compares the relative cost of bubble sort (O(n^2)) and quicksort (O(n log n))
 * on identically seeded data sets. The example emits timing metrics that can be
 * correlated with trace captures to highlight algorithmic hotspots.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ELEMENT_COUNT 1000
#define MEASUREMENT_RUNS 5

static void bubble_sort(int *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        for (size_t j = 0; j + 1 < length - i; ++j) {
            if (data[j] > data[j + 1]) {
                int tmp = data[j];
                data[j] = data[j + 1];
                data[j + 1] = tmp;
            }
        }
    }
}

static void quicksort_range(int *data, int low, int high) {
    int i = low;
    int j = high;
    int pivot = data[(low + high) / 2];

    while (i <= j) {
        while (data[i] < pivot) {
            ++i;
        }
        while (data[j] > pivot) {
            --j;
        }
        if (i <= j) {
            int tmp = data[i];
            data[i] = data[j];
            data[j] = tmp;
            ++i;
            --j;
        }
    }

    if (low < j) {
        quicksort_range(data, low, j);
    }
    if (i < high) {
        quicksort_range(data, i, high);
    }
}

static void quicksort(int *data, size_t length) {
    if (length == 0) {
        return;
    }
    quicksort_range(data, 0, (int)length - 1);
}

static int is_sorted(const int *data, size_t length) {
    for (size_t i = 1; i < length; ++i) {
        if (data[i - 1] > data[i]) {
            return 0;
        }
    }
    return 1;
}

static double measure_sort(void (*sort_fn)(int *, size_t), const int *input, size_t length) {
    double total_us = 0.0;
    int *buffer = (int *)malloc(length * sizeof(int));
    if (buffer == NULL) {
        fprintf(stderr, "Allocation failure during measurement.\n");
        exit(EXIT_FAILURE);
    }

    for (int run = 0; run < MEASUREMENT_RUNS; ++run) {
        memcpy(buffer, input, length * sizeof(int));
        clock_t start = clock();
        sort_fn(buffer, length);
        clock_t end = clock();
        total_us += ((double)(end - start) * 1000000.0) / (double)CLOCKS_PER_SEC;
    }

    if (!is_sorted(buffer, length)) {
        fprintf(stderr, "Sorted output validation failed.\n");
        free(buffer);
        exit(EXIT_FAILURE);
    }

    free(buffer);
    return total_us / (double)MEASUREMENT_RUNS;
}

static void populate_dataset(int *data, size_t length) {
    unsigned int seed = 42U;
    for (size_t i = 0; i < length; ++i) {
        seed = seed * 1664525U + 1013904223U;
        data[i] = (int)(seed & 0x7FFFFFFF);
    }
}

int main(void) {
    int baseline[ELEMENT_COUNT];
    populate_dataset(baseline, ELEMENT_COUNT);

    puts("Performance profiling example: contrasting bubble sort and quicksort.");

    double bubble_us = measure_sort(bubble_sort, baseline, ELEMENT_COUNT);
    double quick_us = measure_sort(quicksort, baseline, ELEMENT_COUNT);

    printf("Bubble sort average: %.2f microseconds\n", bubble_us);
    printf("Quicksort average: %.2f microseconds\n", quick_us);

    if (bubble_us < quick_us) {
        puts("Warning: bubble sort measured faster than quicksort in this run.");
    }

    puts("Arrays sorted and verified.");
    puts("Use a tracer to inspect where time is spent in each algorithm.");

    return 0;
}

