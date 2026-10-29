#ifndef ROUTER_RX_PIPELINE_H_
#define ROUTER_RX_PIPELINE_H_

#include <stdint.h>
#include "router_frame_parser.h"

/* Fixed-memory SPSC queue; 256 records = 2 KiB on typical 32-bit targets.
 * Each record carries ISR reception time so parser timeouts reflect the wire
 * instead of the time a busy foreground finally drains its queue.
 */
#define ROUTER_RX_QUEUE_CAPACITY 256U
#define ROUTER_RX_MAX_BYTES_PER_POLL 96U
#define ROUTER_RX_SESSION_IDLE_MS 5000U

typedef struct {
    uint32_t received_ms;
    uint8_t byte;
} RouterRxRecord;

typedef struct {
    RouterTelemetry telemetry;
    uint32_t sequence;
    uint32_t received_ms;
} RouterRxSample;

typedef struct {
    uint32_t polls;
    uint32_t bytes_processed;
    uint32_t frames_decoded;
    uint32_t samples_delivered;
    uint32_t duplicates_dropped;
    uint32_t old_sequences_dropped;
    uint32_t sequence_gap_events;
    uint32_t stale_frames_dropped;
    uint32_t idle_session_resets;
    uint32_t queue_loss_resets;
} RouterRxPipelineStats;

typedef struct {
    RouterRxRecord queue[ROUTER_RX_QUEUE_CAPACITY];
    uint32_t producer;              /* Only ISR writes; atomically published. */
    uint32_t consumer;              /* Only foreground writes. */
    uint32_t bytes_dropped;         /* Only ISR increments. */
    uint32_t observed_dropped;
    RouterFrameParser parser;       /* Foreground-only. */
    RouterRxPipelineStats stats;    /* Foreground-only. */
    uint32_t last_sequence;
    uint32_t last_sample_ms;
    uint8_t have_sequence;
} RouterRxPipeline;

/* Use before enabling a receive IRQ. Not safe concurrently with an active ISR.
 * Requires GCC/Clang __atomic builtins, supported by STM32CubeIDE ARM GCC.
 * Exactly one producer (ISR) and one consumer (foreground); never call Feed
 * from two interrupt sources or from both IRQ and foreground.
 */
void RouterRxPipeline_Init(RouterRxPipeline *pipeline);

/* ISR-only: O(1), no allocation, parsing, HAL calls or Dashboard updates.
 * 'received_ms' should be captured at byte reception, e.g. HAL_GetTick().
 * Returns 0 if full; count drops and resync in foreground. */
uint8_t RouterRxPipeline_QueueByteFromIsr(RouterRxPipeline *pipeline,
                                          uint8_t byte, uint32_t received_ms);

/* Foreground-only. Drains at most min(byte_budget, 96) bytes per call.
 * Returns 1 with newest valid forward-sequence sample, else 0 leaving out
 * untouched. Call even when idle to expire partial frames and sessions.
 * Output sample is NOT published automatically: caller must use
 * RouterDataProvider_PublishExternal() in foreground with received_ms.
 */
uint8_t RouterRxPipeline_Poll(RouterRxPipeline *pipeline, uint32_t now_ms,
                              uint32_t byte_budget, RouterRxSample *out);

/* Foreground-only, for debug/telemetry; queue count can change during read. */
uint32_t RouterRxPipeline_PendingBytes(const RouterRxPipeline *pipeline);

#endif /* ROUTER_RX_PIPELINE_H_ */
