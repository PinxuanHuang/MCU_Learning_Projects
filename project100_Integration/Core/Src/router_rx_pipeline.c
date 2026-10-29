#include "router_rx_pipeline.h"
#include <stddef.h>

#if !defined(__GNUC__) && !defined(__clang__)
#error "router_rx_pipeline requires GCC/Clang atomic builtins"
#endif

#if ((ROUTER_RX_QUEUE_CAPACITY & (ROUTER_RX_QUEUE_CAPACITY - 1U)) != 0U)
#error "ROUTER_RX_QUEUE_CAPACITY must be a power of two"
#endif

void RouterRxPipeline_Init(RouterRxPipeline *pipeline)
{
    if (pipeline == NULL) return;
    *pipeline = (RouterRxPipeline){0};
    RouterFrameParser_Init(&pipeline->parser);
}

uint8_t RouterRxPipeline_QueueByteFromIsr(RouterRxPipeline *pipeline,
                                          uint8_t byte, uint32_t received_ms)
{
    if (pipeline == NULL) return 0U;
    const uint32_t head = __atomic_load_n(&pipeline->producer, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&pipeline->consumer, __ATOMIC_ACQUIRE);
    if ((uint32_t)(head - tail) >= ROUTER_RX_QUEUE_CAPACITY) {
        /* Single producer; no other context writes this counter. */
        __atomic_store_n(&pipeline->bytes_dropped,
            __atomic_load_n(&pipeline->bytes_dropped, __ATOMIC_RELAXED) + 1U,
            __ATOMIC_RELEASE);
        return 0U;
    }
    RouterRxRecord *record = &pipeline->queue[head & (ROUTER_RX_QUEUE_CAPACITY - 1U)];
    record->received_ms = received_ms;
    record->byte = byte;
    /* The record is visible to the consumer only after this release store. */
    __atomic_store_n(&pipeline->producer, head + 1U, __ATOMIC_RELEASE);
    return 1U;
}

uint32_t RouterRxPipeline_PendingBytes(const RouterRxPipeline *pipeline)
{
    if (pipeline == NULL) return 0U;
    const uint32_t head = __atomic_load_n(&pipeline->producer, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&pipeline->consumer, __ATOMIC_ACQUIRE);
    return (uint32_t)(head - tail);
}

static void recover_from_loss(RouterRxPipeline *pipeline, uint32_t dropped)
{
    /* Discard the queued stream after a lost byte: blindly concatenating
     * pre- and post-overflow bytes could create a synthetic valid frame. */
    const uint32_t head = __atomic_load_n(&pipeline->producer, __ATOMIC_ACQUIRE);
    __atomic_store_n(&pipeline->consumer, head, __ATOMIC_RELEASE);
    pipeline->observed_dropped = dropped;
    pipeline->parser.used = 0U;
    pipeline->parser.have_sequence = 0U;
    pipeline->have_sequence = 0U;
    ++pipeline->stats.queue_loss_resets;
}

uint8_t RouterRxPipeline_Poll(RouterRxPipeline *pipeline, uint32_t now_ms,
                              uint32_t byte_budget, RouterRxSample *out)
{
    if (pipeline == NULL || out == NULL) return 0U;
    ++pipeline->stats.polls;

    uint32_t dropped = __atomic_load_n(&pipeline->bytes_dropped, __ATOMIC_ACQUIRE);
    if (dropped != pipeline->observed_dropped) {
        recover_from_loss(pipeline, dropped);
        return 0U;
    }

    /* New boot/session after a sufficiently long idle period. The same
     * wrap-safe unsigned subtraction is valid if polls are regular. */
    if (pipeline->have_sequence != 0U &&
        (uint32_t)(now_ms - pipeline->last_sample_ms) >= ROUTER_RX_SESSION_IDLE_MS) {
        pipeline->have_sequence = 0U;
        pipeline->parser.have_sequence = 0U;
        ++pipeline->stats.idle_session_resets;
    }

    if (byte_budget > ROUTER_RX_MAX_BYTES_PER_POLL)
        byte_budget = ROUTER_RX_MAX_BYTES_PER_POLL;

    uint8_t have_output = 0U;
    RouterRxSample latest = {0};
    for (uint32_t i = 0U; i < byte_budget; ++i) {
        const uint32_t tail = __atomic_load_n(&pipeline->consumer, __ATOMIC_RELAXED);
        const uint32_t head = __atomic_load_n(&pipeline->producer, __ATOMIC_ACQUIRE);
        if (tail == head) break;

        const RouterRxRecord record =
            pipeline->queue[tail & (ROUTER_RX_QUEUE_CAPACITY - 1U)];
        __atomic_store_n(&pipeline->consumer, tail + 1U, __ATOMIC_RELEASE);
        ++pipeline->stats.bytes_processed;

        RouterFrame frame;
        if (RouterFrameParser_Feed(&pipeline->parser, record.byte,
                                   record.received_ms, &frame) == 0U)
            continue;
        ++pipeline->stats.frames_decoded;

        /* Never turn a delayed backlog into fresh Router telemetry. */
        if ((uint32_t)(now_ms - record.received_ms) >= ROUTER_RX_SESSION_IDLE_MS) {
            ++pipeline->stats.stale_frames_dropped;
            continue;
        }

        if (pipeline->have_sequence != 0U) {
            const uint32_t delta = frame.sequence - pipeline->last_sequence;
            if (delta == 0U) {
                ++pipeline->stats.duplicates_dropped;
                continue;
            }
            if (delta >= UINT32_C(0x80000000)) {
                ++pipeline->stats.old_sequences_dropped;
                continue;
            }
            if (delta > 1U) ++pipeline->stats.sequence_gap_events;
        }
        pipeline->last_sequence = frame.sequence;
        pipeline->last_sample_ms = record.received_ms;
        pipeline->have_sequence = 1U;
        latest.telemetry = frame.telemetry;
        latest.sequence = frame.sequence;
        latest.received_ms = record.received_ms;
        have_output = 1U;
    }

    /* Do not expire a partial frame while there are still queued bytes.
     * Their original timestamps are checked by Feed when drained. */
    if (RouterRxPipeline_PendingBytes(pipeline) == 0U)
        RouterFrameParser_Poll(&pipeline->parser, now_ms);

    /* An overflow racing with this call invalidates any tentative output. */
    dropped = __atomic_load_n(&pipeline->bytes_dropped, __ATOMIC_ACQUIRE);
    if (dropped != pipeline->observed_dropped) {
        recover_from_loss(pipeline, dropped);
        return 0U;
    }
    if (have_output != 0U) {
        *out = latest;
        ++pipeline->stats.samples_delivered;
    }
    return have_output;
}
