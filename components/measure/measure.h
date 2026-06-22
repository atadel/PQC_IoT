//Author: Aleksandra Tadel

#ifndef PQC_MEASURE_H
#define PQC_MEASURE_H

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    uint32_t start_cycles;
    int64_t  start_time;
    size_t   heap_before, heap_total, heap_min, heap_min_before;
    uint32_t stack_before;
    const char* name;
    int iteration;
} pqc_metric_t;

void start_trace();
void stop_trace();
void start_measure(const char* name);
void end_measure(void);
void save_result(const char* name,
                 uint32_t cycles,
                 int64_t time_us,
                 uint32_t stack,
                 size_t heap_event);
void export_csv(void);
void reset_measurement_results(void);
void set_current_iteration(int iter);
void reset_heap_monitor_between_iterations(void);

// Function-based API: use start_measure() / end_measure()
// (Macros are intentionally omitted to avoid redefinition
// conflicts with other components that may define MEASURE_*.)

#endif