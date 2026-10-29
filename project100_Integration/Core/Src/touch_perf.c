/* M2.5 - read-only touch to dashboard DMA observability.
 * All calls are main-loop only. This module never drives LCD or XPT2046.
 */
#include "touch_perf.h"
#include "touch_raw.h"
#include "dashboard_perf.h"
#include <stddef.h>

#define TOUCH_PERF_SOAK_DURATION_MS 1800000UL

volatile TouchPerfStats touch_perf;
static uint32_t s_boot_ms;
static uint32_t s_pending_since_ms;
static uint8_t s_pending;

void TouchPerf_Init(uint32_t now_ms)
{
    touch_perf = (TouchPerfStats){0};
    s_boot_ms = now_ms;
    s_pending_since_ms = now_ms;
    s_pending = 0U;
}

void TouchPerf_OnInputService(uint32_t start_cycles, uint32_t end_cycles)
{
    touch_perf.input_service_calls++;
    if (dashboard_perf.dwt_available != 0U)
    {
        /* Unsigned subtraction handles CYCCNT wrap for short input services. */
        uint32_t cycles = end_cycles - start_cycles;
        touch_perf.input_last_cycles = cycles;
        if (cycles > touch_perf.input_max_cycles)
            touch_perf.input_max_cycles = cycles;
        touch_perf.input_total_cycles += cycles;
    }
    /* Mirrored existing counters: safe even without an M2.3 UI bridge. */
    touch_perf.raw_polls = touch_raw_stats.polls;
    touch_perf.raw_samples_ok = touch_raw_stats.samples_ok;
    touch_perf.raw_invalid_samples = touch_raw_stats.invalid_samples;
    touch_perf.pen_down_edges = touch_raw_stats.pen_down_edges;
    touch_perf.pen_up_edges = touch_raw_stats.pen_up_edges;
    touch_perf.button_presses = touch_input_stats.button_presses;
    touch_perf.events_dropped = touch_input_stats.events_dropped;
}

void TouchPerf_OnAction(const TouchInputEvent *event,
                           TouchInputTarget target, uint32_t now_ms)
{
    if (event == NULL || target == TOUCH_INPUT_TARGET_NONE) return;
    if (event->source != TOUCH_INPUT_SOURCE_TOUCH)
    {
        if (event->source == TOUCH_INPUT_SOURCE_BUTTON_SIM)
            touch_perf.button_actions++;
        return;
    }
    touch_perf.touch_actions++;
    switch (target)
    {
        case TOUCH_INPUT_TARGET_RX:  touch_perf.touch_rx++; break;
        case TOUCH_INPUT_TARGET_TX:  touch_perf.touch_tx++; break;
        case TOUCH_INPUT_TARGET_CPU: touch_perf.touch_cpu++; break;
        case TOUCH_INPUT_TARGET_RAM: touch_perf.touch_ram++; break;
        default: break;
    }
    /* tick_ms is the software-gesture release time, NOT physical IRQ time. */
    uint32_t queue_ms = now_ms - event->tick_ms;
    touch_perf.queue_to_dispatch_last_ms = queue_ms;
    if (queue_ms > touch_perf.queue_to_dispatch_max_ms)
        touch_perf.queue_to_dispatch_max_ms = queue_ms;
    if (s_pending != 0U)
    {
        /* Latest-wins Dashboard_Update may coalesce several touch actions. */
        touch_perf.touch_actions_coalesced++;
        return;
    }
    s_pending = 1U;
    s_pending_since_ms = event->tick_ms;
    touch_perf.touch_batch_pending = 1U;
    touch_perf.touch_batches_started++;
}

void TouchPerf_Poll(uint32_t now_ms, uint8_t dashboard_busy,
                       uint8_t dma_fault)
{
    touch_perf.soak_elapsed_ms = now_ms - s_boot_ms;
    if (s_pending != 0U)
    {
        uint32_t elapsed = now_ms - s_pending_since_ms;
        if (elapsed > touch_perf.longest_pending_ms)
            touch_perf.longest_pending_ms = elapsed;
        if (dma_fault != 0U)
        {
            touch_perf.touch_batches_aborted++;
            s_pending = 0U;
            touch_perf.touch_batch_pending = 0U;
        }
        else if (dashboard_busy == 0U)
        {
            /* All queued dashboard work reached IDLE, not LCD scanout end. */
            touch_perf.render_last_ms = elapsed;
            if (elapsed > touch_perf.render_max_ms)
                touch_perf.render_max_ms = elapsed;
            touch_perf.render_total_ms += elapsed;
            touch_perf.touch_batches_completed++;
            s_pending = 0U;
            touch_perf.touch_batch_pending = 0U;
        }
    }
    if (touch_perf.soak_elapsed_ms >= TOUCH_PERF_SOAK_DURATION_MS &&
        touch_perf.touch_actions > 0U &&
        touch_perf.touch_batches_completed > 0U &&
        touch_perf.events_dropped == 0U &&
        touch_perf.touch_batches_aborted == 0U &&
        dashboard_perf.dma_fault_count == 0U)
    {
        touch_perf.soak_30min_pass = 1U;
    }
    /* Once set, soak PASS is latched: a late failure must invalidate it. */
    if (touch_perf.events_dropped != 0U ||
        touch_perf.touch_batches_aborted != 0U ||
        dashboard_perf.dma_fault_count != 0U)
        touch_perf.soak_30min_pass = 0U;
}
