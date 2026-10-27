/* M2.5 - read-only touch to dashboard DMA observability.
 * All calls are main-loop only. This module never drives LCD or XPT2046.
 */
#include "m2_touch_perf.h"
#include "m2_touch_raw.h"
#include "m14_perf.h"
#include <stddef.h>

#define M2_PERF_SOAK_DURATION_MS 1800000UL

volatile M2_TouchPerfStats m2_touch_perf;
static uint32_t s_boot_ms;
static uint32_t s_pending_since_ms;
static uint8_t s_pending;

void M2_TouchPerf_Init(uint32_t now_ms)
{
    m2_touch_perf = (M2_TouchPerfStats){0};
    s_boot_ms = now_ms;
    s_pending_since_ms = now_ms;
    s_pending = 0U;
}

void M2_TouchPerf_OnInputService(uint32_t start_cycles, uint32_t end_cycles)
{
    m2_touch_perf.input_service_calls++;
    if (m14_perf.dwt_available != 0U)
    {
        /* Unsigned subtraction handles CYCCNT wrap for short input services. */
        uint32_t cycles = end_cycles - start_cycles;
        m2_touch_perf.input_last_cycles = cycles;
        if (cycles > m2_touch_perf.input_max_cycles)
            m2_touch_perf.input_max_cycles = cycles;
        m2_touch_perf.input_total_cycles += cycles;
    }
    /* Mirrored existing counters: safe even without an M2.3 UI bridge. */
    m2_touch_perf.raw_polls = m2_touch_raw_stats.polls;
    m2_touch_perf.raw_samples_ok = m2_touch_raw_stats.samples_ok;
    m2_touch_perf.raw_invalid_samples = m2_touch_raw_stats.invalid_samples;
    m2_touch_perf.pen_down_edges = m2_touch_raw_stats.pen_down_edges;
    m2_touch_perf.pen_up_edges = m2_touch_raw_stats.pen_up_edges;
    m2_touch_perf.button_presses = m2_input_stats.button_presses;
    m2_touch_perf.events_dropped = m2_input_stats.events_dropped;
}

void M2_TouchPerf_OnAction(const M2_InputEvent *event,
                           M2_InputTarget target, uint32_t now_ms)
{
    if (event == NULL || target == M2_INPUT_TARGET_NONE) return;
    if (event->source != M2_INPUT_SOURCE_TOUCH)
    {
        if (event->source == M2_INPUT_SOURCE_BUTTON_SIM)
            m2_touch_perf.button_actions++;
        return;
    }
    m2_touch_perf.touch_actions++;
    switch (target)
    {
        case M2_INPUT_TARGET_RX:  m2_touch_perf.touch_rx++; break;
        case M2_INPUT_TARGET_TX:  m2_touch_perf.touch_tx++; break;
        case M2_INPUT_TARGET_CPU: m2_touch_perf.touch_cpu++; break;
        case M2_INPUT_TARGET_RAM: m2_touch_perf.touch_ram++; break;
        default: break;
    }
    /* tick_ms is the software-gesture release time, NOT physical IRQ time. */
    uint32_t queue_ms = now_ms - event->tick_ms;
    m2_touch_perf.queue_to_dispatch_last_ms = queue_ms;
    if (queue_ms > m2_touch_perf.queue_to_dispatch_max_ms)
        m2_touch_perf.queue_to_dispatch_max_ms = queue_ms;
    if (s_pending != 0U)
    {
        /* Latest-wins Dashboard_Update may coalesce several touch actions. */
        m2_touch_perf.touch_actions_coalesced++;
        return;
    }
    s_pending = 1U;
    s_pending_since_ms = event->tick_ms;
    m2_touch_perf.touch_batch_pending = 1U;
    m2_touch_perf.touch_batches_started++;
}

void M2_TouchPerf_Poll(uint32_t now_ms, uint8_t dashboard_busy,
                       uint8_t dma_fault)
{
    m2_touch_perf.soak_elapsed_ms = now_ms - s_boot_ms;
    if (s_pending != 0U)
    {
        uint32_t elapsed = now_ms - s_pending_since_ms;
        if (elapsed > m2_touch_perf.longest_pending_ms)
            m2_touch_perf.longest_pending_ms = elapsed;
        if (dma_fault != 0U)
        {
            m2_touch_perf.touch_batches_aborted++;
            s_pending = 0U;
            m2_touch_perf.touch_batch_pending = 0U;
        }
        else if (dashboard_busy == 0U)
        {
            /* All queued dashboard work reached IDLE, not LCD scanout end. */
            m2_touch_perf.render_last_ms = elapsed;
            if (elapsed > m2_touch_perf.render_max_ms)
                m2_touch_perf.render_max_ms = elapsed;
            m2_touch_perf.render_total_ms += elapsed;
            m2_touch_perf.touch_batches_completed++;
            s_pending = 0U;
            m2_touch_perf.touch_batch_pending = 0U;
        }
    }
    if (m2_touch_perf.soak_elapsed_ms >= M2_PERF_SOAK_DURATION_MS &&
        m2_touch_perf.touch_actions > 0U &&
        m2_touch_perf.touch_batches_completed > 0U &&
        m2_touch_perf.events_dropped == 0U &&
        m2_touch_perf.touch_batches_aborted == 0U &&
        m14_perf.dma_fault_count == 0U)
    {
        m2_touch_perf.soak_30min_pass = 1U;
    }
    /* Once set, soak PASS is latched: a late failure must invalidate it. */
    if (m2_touch_perf.events_dropped != 0U ||
        m2_touch_perf.touch_batches_aborted != 0U ||
        m14_perf.dma_fault_count != 0U)
        m2_touch_perf.soak_30min_pass = 0U;
}
