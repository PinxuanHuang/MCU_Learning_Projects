/*
 * M1.4.6 - Diagnostic-only performance counters.
 * DWT CYCCNT measures CORE clock cycles, with 32-bit wrap handled by
 * unsigned subtraction for each short (< 2^32 cycles) operation.
 * Caller: foreground main loop, except RecordDmaIrq() in DMA1 Stream4 ISR.
 * No DMA, SPI, GPIO, or display operations occur in this module.
 */
#include "dashboard_perf.h"
#include "main.h"   /* STM32F4 CMSIS CoreDebug, DWT, SystemCoreClock */

#define SOAK_DURATION_MS 1800000U

volatile DashboardPerfStats dashboard_perf;
static uint32_t s_boot_ms;
static uint32_t s_batch_start_ms;
static uint8_t s_batch_pending;
static uint8_t s_fault_latched;

void DashboardPerf_Init(uint32_t now_ms)
{
    /* Intended once, AFTER SystemClock_Config and startup DMA self-tests. */
    dashboard_perf.dwt_available = 0U;
    dashboard_perf.core_clock_hz = SystemCoreClock;
    dashboard_perf.uptime_ms = 0U;
    dashboard_perf.soak_30min_pass = 0U;
    dashboard_perf.update_requests = 0U;
    dashboard_perf.coalesced_requests = 0U;
    dashboard_perf.batches_completed = 0U;
    dashboard_perf.batches_aborted = 0U;
    dashboard_perf.batch_last_ms = 0U;
    dashboard_perf.batch_max_ms = 0U;
    dashboard_perf.batch_total_ms = 0U;
    dashboard_perf.service_calls = 0U;
    dashboard_perf.service_last_cycles = 0U;
    dashboard_perf.service_max_cycles = 0U;
    dashboard_perf.service_total_cycles = 0U;
    dashboard_perf.dma_irq_count = 0U;
    dashboard_perf.dma_irq_last_cycles = 0U;
    dashboard_perf.dma_irq_max_cycles = 0U;
    dashboard_perf.dma_irq_total_cycles = 0U;
    dashboard_perf.dma_fault_count = 0U;

    s_boot_ms = now_ms;
    s_batch_start_ms = now_ms;
    s_batch_pending = 0U;
    s_fault_latched = 0U;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U)
        dashboard_perf.dwt_available = 1U;
}

uint32_t DashboardPerf_CycleNow(void)
{
    return dashboard_perf.dwt_available ? DWT->CYCCNT : 0U;
}

void DashboardPerf_RecordService(uint32_t start_cycles)
{
    /* Counts every invocation, even if CYCCNT isn't available. */
    dashboard_perf.service_calls++;
    if (!dashboard_perf.dwt_available) return;
    uint32_t delta = DWT->CYCCNT - start_cycles;
    dashboard_perf.service_last_cycles = delta;
    if (delta > dashboard_perf.service_max_cycles)
        dashboard_perf.service_max_cycles = delta;
    dashboard_perf.service_total_cycles += delta;
}

void DashboardPerf_RecordDmaIrq(uint32_t start_cycles)
{
    dashboard_perf.dma_irq_count++;
    if (!dashboard_perf.dwt_available) return;
    uint32_t delta = DWT->CYCCNT - start_cycles;
    dashboard_perf.dma_irq_last_cycles = delta;
    if (delta > dashboard_perf.dma_irq_max_cycles)
        dashboard_perf.dma_irq_max_cycles = delta;
    dashboard_perf.dma_irq_total_cycles += delta;
}

void DashboardPerf_OnStatsQueued(uint32_t now_ms)
{
    dashboard_perf.update_requests++;
    if (s_batch_pending)
    {
        dashboard_perf.coalesced_requests++;
        return; /* Latest stats win; retain first enqueue-to-idle origin. */
    }
    s_batch_start_ms = now_ms;
    s_batch_pending = 1U;
}

void DashboardPerf_Poll(uint32_t now_ms, uint8_t dashboard_busy, uint8_t dma_fault)
{
    dashboard_perf.uptime_ms = (uint32_t)(now_ms - s_boot_ms);
    if (dma_fault)
    {
        if (!s_fault_latched)
        {
            dashboard_perf.dma_fault_count++;
            dashboard_perf.soak_30min_pass = 0U; /* A later fault invalidates soak health. */
            if (s_batch_pending)
            {
                dashboard_perf.batches_aborted++;
                s_batch_pending = 0U;
            }
        }
        s_fault_latched = 1U;
        return;
    }
    s_fault_latched = 0U;
    if (s_batch_pending && !dashboard_busy)
    {
        uint32_t elapsed_ms = (uint32_t)(now_ms - s_batch_start_ms);
        dashboard_perf.batch_last_ms = elapsed_ms;
        if (elapsed_ms > dashboard_perf.batch_max_ms)
            dashboard_perf.batch_max_ms = elapsed_ms;
        dashboard_perf.batch_total_ms += elapsed_ms;
        dashboard_perf.batches_completed++;
        s_batch_pending = 0U;
    }
    if (dashboard_perf.uptime_ms >= SOAK_DURATION_MS &&
        dashboard_perf.dma_fault_count == 0U && dashboard_perf.batches_completed > 0U)
        dashboard_perf.soak_30min_pass = 1U;
}
