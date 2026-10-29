#include "router_frame_parser.h"
#include <string.h>

void RouterFrameParser_Init(RouterFrameParser *parser)
{
    if (parser != NULL) *parser = (RouterFrameParser){0};
}
static void discard_first(RouterFrameParser *parser)
{
    --parser->used;
    memmove(parser->buffer, parser->buffer + 1, parser->used);
    ++parser->stats.discarded_bytes;
}
void RouterFrameParser_Poll(RouterFrameParser *parser, uint32_t now_ms)
{
    if (parser != NULL && parser->used != 0U &&
        (uint32_t)(now_ms - parser->last_byte_ms) >= ROUTER_FRAME_TIMEOUT_MS) {
        parser->stats.discarded_bytes += (uint32_t)parser->used;
        parser->used = 0U;
        ++parser->stats.timeouts;
    }
}
static void record_sequence(RouterFrameParser *parser, uint32_t sequence)
{
    if (parser->have_sequence != 0U) {
        uint32_t delta = sequence - parser->last_sequence;
        if (delta == 0U) ++parser->stats.duplicate_sequences;
        else if (delta >= UINT32_C(0x80000000)) ++parser->stats.backward_sequences;
        else if (delta > 1U) ++parser->stats.sequence_gap_events;
    }
    /* Compare consecutive arrivals, not a monotonic high-water mark. */
    parser->last_sequence = sequence;
    parser->have_sequence = 1U;
}
uint8_t RouterFrameParser_Feed(RouterFrameParser *parser, uint8_t byte,
                              uint32_t now_ms, RouterFrame *out)
{
    if (parser == NULL || out == NULL) return 0U;
    RouterFrameParser_Poll(parser, now_ms);
    parser->last_byte_ms = now_ms;
    ++parser->stats.bytes_received;
    /* Each prior call returns with used < FRAME_SIZE. */
    parser->buffer[parser->used++] = byte;
    while (parser->used != 0U) {
        const uint8_t *b = parser->buffer;
        if (b[0] != ROUTER_PROTOCOL_MAGIC_FIRST ||
            (parser->used >= 2U && b[1] != ROUTER_PROTOCOL_MAGIC_SECOND)) {
            discard_first(parser);
            continue;
        }
        if (parser->used < ROUTER_PROTOCOL_HEADER_SIZE) return 0U;
        if (b[2] != ROUTER_PROTOCOL_VERSION || b[3] != ROUTER_PROTOCOL_TELEMETRY ||
            b[8] != ROUTER_PROTOCOL_PAYLOAD_SIZE || b[9] != 0U) {
            ++parser->stats.header_errors;
            discard_first(parser);
            continue;
        }
        if (parser->used < ROUTER_PROTOCOL_FRAME_SIZE) return 0U;
        RouterProtocolResult result = RouterProtocol_Decode(b, parser->used, out);
        if (result == ROUTER_PROTOCOL_OK) {
            ++parser->stats.frames_received;
            record_sequence(parser, out->sequence);
            parser->used = 0U;
            return 1U;
        }
        if (result == ROUTER_PROTOCOL_CRC) ++parser->stats.crc_errors;
        else if (result == ROUTER_PROTOCOL_RANGE) ++parser->stats.range_errors;
        else ++parser->stats.header_errors;
        /* Preserve every possible embedded magic following a corrupt frame. */
        discard_first(parser);
    }
    return 0U;
}
