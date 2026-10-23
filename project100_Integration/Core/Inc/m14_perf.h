/* M1.4.6 - SPI2 DMA / dashboard instrumentation, foreground-only API except IRQ. */
#ifndef M14_PERF_H_
#define M14_PERF_H_

#include <stdint.h>

typedef struct
{
    uint32_t dwt_available;           /* 1: DWT CYCCNT enabled, 0: ms + IRQ counts only */
    uint32_t core_clock_hz;           /* cycles/second */
    uint32_t uptime_ms;               /* since M14_Perf_Init */
    uint32_t soak_30min_pass;         /* automated duration+fault check only */

    uint32_t update_requests;         /* Dashboard_Update calls observed */
    uint32_t coalesced_requests;      /* arrived while previous batch pending */
    uint32_t batches_completed;       /* queue drained, NOT full-display frames */
    uint32_t batches_aborted;         /* pending work abandoned on DMA fault */
    uint32_t batch_last_ms;
    uint32_t batch_max_ms;
    uint64_t batch_total_ms;          /* avg = total / batches_completed */

    uint64_t service_calls;           /* Dashboard_Service calls in foreground */
    uint32_t service_last_cycles;
    uint32_t service_max_cycles;
    uint64_t service_total_cycles;    /* does not include DMA IRQ time */

    uint32_t dma_irq_count;           /* DMA1 Stream4 IRQ entries after Init */
    uint32_t dma_irq_last_cycles;
    uint32_t dma_irq_max_cycles;
    uint64_t dma_irq_total_cycles;    /* HAL handler + callbacks, not whole CPU use */

    uint32_t dma_fault_count;         /* rising edges of fault indication */
} M14_PerfStats;

/* Visible from STM32CubeIDE Debugger Expressions. IRQ fields update in ISR. */
extern volatile M14_PerfStats m14_perf;

void M14_Perf_Init(uint32_t now_ms);
uint32_t M14_Perf_CycleNow(void);
void M14_Perf_RecordService(uint32_t start_cycles);
void M14_Perf_RecordDmaIrq(uint32_t start_cycles);
void M14_Perf_OnStatsQueued(uint32_t now_ms);
void M14_Perf_Poll(uint32_t now_ms, uint8_t dashboard_busy, uint8_t dma_fault);

#endif /* M14_PERF_H_ */
