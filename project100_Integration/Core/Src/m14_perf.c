/*
 * M1.4.6 - Diagnostic-only performance counters.
 * DWT CYCCNT measures CORE clock cycles, with 32-bit wrap handled by
 * unsigned subtraction for each short (< 2^32 cycles) operation.
 * Caller: foreground main loop, except RecordDmaIrq() in DMA1 Stream4 ISR.
 * No DMA, SPI, GPIO, or display operations occur in this module.
 */
#include "m14_perf.h"
#include "main.h"   /* STM32F4 CMSIS CoreDebug, DWT, SystemCoreClock */

#define M14_SOAK_DURATION_MS 1800000U

volatile M14_PerfStats m14_perf;
static uint32_t s_boot_ms;
static uint32_t s_batch_start_ms;
static uint8_t s_batch_pending;
static uint8_t s_fault_latched;

void M14_Perf_Init(uint32_t now_ms)
{
    /* Intended once, AFTER SystemClock_Config and startup DMA self-tests. */
    m14_perf.dwt_available = 0U;
    m14_perf.core_clock_hz = SystemCoreClock;
    m14_perf.uptime_ms = 0U;
    m14_perf.soak_30min_pass = 0U;
    m14_perf.update_requests = 0U;
    m14_perf.coalesced_requests = 0U;
    m14_perf.batches_completed = 0U;
    m14_perf.batches_aborted = 0U;
    m14_perf.batch_last_ms = 0U;
    m14_perf.batch_max_ms = 0U;
    m14_perf.batch_total_ms = 0U;
    m14_perf.service_calls = 0U;
    m14_perf.service_last_cycles = 0U;
    m14_perf.service_max_cycles = 0U;
    m14_perf.service_total_cycles = 0U;
    m14_perf.dma_irq_count = 0U;
    m14_perf.dma_irq_last_cycles = 0U;
    m14_perf.dma_irq_max_cycles = 0U;
    m14_perf.dma_irq_total_cycles = 0U;
    m14_perf.dma_fault_count = 0U;

    s_boot_ms = now_ms;
    s_batch_start_ms = now_ms;
    s_batch_pending = 0U;
    s_fault_latched = 0U;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U)
        m14_perf.dwt_available = 1U;
}

uint32_t M14_Perf_CycleNow(void)
{
    return m14_perf.dwt_available ? DWT->CYCCNT : 0U;
}

void M14_Perf_RecordService(uint32_t start_cycles)
{
    /* Counts every invocation, even if CYCCNT isn't available. */
    m14_perf.service_calls++;
    if (!m14_perf.dwt_available) return;
    uint32_t delta = DWT->CYCCNT - start_cycles;
    m14_perf.service_last_cycles = delta;
    if (delta > m14_perf.service_max_cycles)
        m14_perf.service_max_cycles = delta;
    m14_perf.service_total_cycles += delta;
}

void M14_Perf_RecordDmaIrq(uint32_t start_cycles)
{
    m14_perf.dma_irq_count++;
    if (!m14_perf.dwt_available) return;
    uint32_t delta = DWT->CYCCNT - start_cycles;
    m14_perf.dma_irq_last_cycles = delta;
    if (delta > m14_perf.dma_irq_max_cycles)
        m14_perf.dma_irq_max_cycles = delta;
    m14_perf.dma_irq_total_cycles += delta;
}

void M14_Perf_OnStatsQueued(uint32_t now_ms)
{
    m14_perf.update_requests++;
    if (s_batch_pending)
    {
        m14_perf.coalesced_requests++;
        return; /* Latest stats win; retain first enqueue-to-idle origin. */
    }
    s_batch_start_ms = now_ms;
    s_batch_pending = 1U;
}

void M14_Perf_Poll(uint32_t now_ms, uint8_t dashboard_busy, uint8_t dma_fault)
{
    m14_perf.uptime_ms = (uint32_t)(now_ms - s_boot_ms);
    if (dma_fault)
    {
        if (!s_fault_latched)
        {
            m14_perf.dma_fault_count++;
            m14_perf.soak_30min_pass = 0U; /* A later fault invalidates soak health. */
            if (s_batch_pending)
            {
                m14_perf.batches_aborted++;
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
        m14_perf.batch_last_ms = elapsed_ms;
        if (elapsed_ms > m14_perf.batch_max_ms)
            m14_perf.batch_max_ms = elapsed_ms;
        m14_perf.batch_total_ms += elapsed_ms;
        m14_perf.batches_completed++;
        s_batch_pending = 0U;
    }
    if (m14_perf.uptime_ms >= M14_SOAK_DURATION_MS &&
        m14_perf.dma_fault_count == 0U && m14_perf.batches_completed > 0U)
        m14_perf.soak_30min_pass = 1U;
}
