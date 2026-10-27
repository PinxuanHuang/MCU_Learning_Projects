/* M2.5 - foreground-only touch/DMA end-to-end performance diagnostics.
 * No SPI, GPIO, Flash or DMA writes. This module observes existing services.
 */
#ifndef M2_TOUCH_PERF_H_
#define M2_TOUCH_PERF_H_

#include <stdint.h>
#include "m2_input.h"

typedef struct {
    uint32_t input_service_calls;
    uint32_t input_last_cycles;
    uint32_t input_max_cycles;
    uint64_t input_total_cycles;
    uint32_t raw_polls;
    uint32_t raw_samples_ok;
    uint32_t raw_invalid_samples;
    uint32_t pen_down_edges;
    uint32_t pen_up_edges;
    uint32_t button_presses;
    uint32_t events_dropped;
    uint32_t touch_actions;
    uint32_t button_actions;
    uint32_t touch_rx;
    uint32_t touch_tx;
    uint32_t touch_cpu;
    uint32_t touch_ram;
    uint32_t queue_to_dispatch_last_ms;
    uint32_t queue_to_dispatch_max_ms;
    uint32_t touch_batches_started;
    uint32_t touch_batches_completed;
    uint32_t touch_actions_coalesced;
    uint32_t touch_batches_aborted;
    uint32_t render_last_ms;
    uint32_t render_max_ms;
    uint64_t render_total_ms;
    uint32_t longest_pending_ms;
    uint32_t soak_elapsed_ms;
    uint8_t touch_batch_pending;
    uint8_t soak_30min_pass;
} M2_TouchPerfStats;

extern volatile M2_TouchPerfStats m2_touch_perf;

/* Initialize after M14_Perf_Init and after touch input initialization. */
void M2_TouchPerf_Init(uint32_t now_ms);
/* Bracket M2_Input_Service + raw touch + calibration + UI bridge services. */
void M2_TouchPerf_OnInputService(uint32_t start_cycles, uint32_t end_cycles);
/* Call ONLY when a classified input action enqueues Dashboard_Update(). */
void M2_TouchPerf_OnAction(const M2_InputEvent *event,
                           M2_InputTarget target, uint32_t now_ms);
/* Call after Dashboard_Service, event consumption, and Dashboard_Update.
 * dashboard_busy must use Dashboard_IsBusy() (checks active/queued DMA).
 */
void M2_TouchPerf_Poll(uint32_t now_ms, uint8_t dashboard_busy,
                       uint8_t dma_fault);

#endif /* M2_TOUCH_PERF_H_ */
