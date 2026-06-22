//Author: Aleksandra Tadel

#include "esp_heap_trace.h"
#include "measure.h"
#include <stdio.h>
#include "esp_cpu.h"

static int stack_size = 24*1024; // 24 kB - stack size used in mqtt_task, adjust as needed

#define MAX_RESULTS 300
static int result_idx = 0;

typedef struct {
    const char* name;
    int iteration;
    uint32_t cycles;
    int64_t time_us;
    uint32_t stack;
    size_t heap_event;
} result_t;

static result_t results[MAX_RESULTS];
static int current_iteration = 0;

void save_result(const char* name,
                 uint32_t cycles,
                 int64_t time_us,
                 uint32_t stack,
                 size_t heap_event)
{
    if (result_idx >= MAX_RESULTS) return;

    results[result_idx++] = (result_t){
        .name = name,
        .iteration = current_iteration,
        .cycles = cycles,
        .time_us = time_us,
        .stack = stack,
        .heap_event = heap_event,
    };
}

void export_csv(void)
{
    printf("iteration,name,cycles,time_us,stack,heap_event\n");

    for (int i = 0; i < result_idx; i++) {
        printf("%d,%s,%lu,%lld,%lu,%u\n",
            results[i].iteration,
            results[i].name,
            (unsigned long)results[i].cycles,
            (long long)results[i].time_us,
            (unsigned long)results[i].stack,
            (unsigned)results[i].heap_event
        );
    }
}

#define MEASURE_STACK_DEPTH 16
static pqc_metric_t measure_stack[MEASURE_STACK_DEPTH];
static int measure_top = 0;

void start_measure(const char* name)
{
    if (measure_top >= MEASURE_STACK_DEPTH)
        measure_top = MEASURE_STACK_DEPTH - 1;

    pqc_metric_t* m = &measure_stack[measure_top++];
    m->name = name;
    //start_trace();
    //m->heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    heap_caps_monitor_local_minimum_free_size_start();
    m->heap_min_before = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    m->heap_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    m->stack_before = uxTaskGetStackHighWaterMark(NULL);
   // m->heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    //heap_caps_monitor_local_minimum_free_size_start();
    m->start_cycles = esp_cpu_get_cycle_count();
    m->start_time = esp_timer_get_time();
}

void end_measure(void)
{
    if (measure_top <= 0)
        return;
    size_t  heap_min_local = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    heap_caps_monitor_local_minimum_free_size_stop();
    size_t  heap_min_global = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    pqc_metric_t* m = &measure_stack[--measure_top];
    uint32_t end_cycles = esp_cpu_get_cycle_count();
    int64_t  end_time = esp_timer_get_time();
    uint32_t stack_after = uxTaskGetStackHighWaterMark(NULL);
    save_result(
    m->name,
    end_cycles - m->start_cycles,
    end_time - m->start_time,
    stack_size - stack_after,
    (m->heap_min_before - heap_min_local)
);
    //size_t heap_diff = m->heap_before - heap_min_after;

    //stop_trace();
    /*
    printf("\n--- [%s] REPORT ---\n", m->name ? m->name : "?");
    printf("Cycles: %lu\n", (unsigned long)(end_cycles - m->start_cycles));
    printf("Time:   %lld us\n", (long long)(end_time - m->start_time));
    //printf("StackMaxBefore: %lu\n", (unsigned long)(stack_size - m->stack_before));
    printf("StackMaxUsage: %ld\n", (long)(stack_size - stack_after));
    //printf("StackEventUsage: %ld\n", (long)m->stack_before - stack_after);
    printf("HeapEventUsage: %d\n", (int)(m->heap_min_before - heap_min_local));
    printf("HeapMaxUsage: %lu\n", (unsigned long)(m->heap_total - heap_min_global));
    //printf("Heap After Min: %lu\n", (unsigned long)heap_min_local);
    printf("Heap Before: %lu\n", (unsigned long)m->heap_before);
    printf("Heap Min Before: %lu\n", (unsigned long)m->heap_min_before);
    printf("Heap After: %lu\n", (unsigned long)heap_min_local);
    //printf("HeapEvent: %d\n", m->heap_before - heap_min_after);
    printf("-------------------\n");
    */
}

void reset_measurement_results(void)
{
    result_idx = 0;
    measure_top = 0;
    current_iteration = 0;
}

void set_current_iteration(int iter)
{
    current_iteration = iter;
}

