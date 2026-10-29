#ifndef ROUTER_FRAME_PARSER_H_
#define ROUTER_FRAME_PARSER_H_
#include "router_protocol.h"
#define ROUTER_FRAME_TIMEOUT_MS 250U

typedef struct {
    uint32_t bytes_received;
    uint32_t frames_received;
    uint32_t discarded_bytes;
    uint32_t header_errors;
    uint32_t crc_errors;
    uint32_t range_errors;
    uint32_t timeouts;
    uint32_t duplicate_sequences;
    uint32_t backward_sequences;
    uint32_t sequence_gap_events;
} RouterFrameParserStats;
typedef struct {
    uint8_t buffer[ROUTER_PROTOCOL_FRAME_SIZE];
    size_t used;
    uint32_t last_byte_ms;
    uint32_t last_sequence;
    uint8_t have_sequence;
    RouterFrameParserStats stats;
} RouterFrameParser;
/* Foreground-only, single owner; no heap, HAL, callbacks or hardware I/O.
 * Init once before use (also resets diagnostics and sequence on reconnect).
 * Null parser is a no-op. Do not modify internals or alias out with parser. */
void RouterFrameParser_Init(RouterFrameParser *parser);
/* Call during idle periods too. uint32 ms arithmetic handles tick wrap;
 * calls must be separated by less than one full uint32 tick period. */
void RouterFrameParser_Poll(RouterFrameParser *parser, uint32_t now_ms);
/* One byte per call, returns 1 only for a valid frame. NULL out consumes
 * nothing. Other returns leave out unchanged. Use a finite byte budget per
 * main-loop iteration. All valid sequences are delivered (diagnostic only). */
uint8_t RouterFrameParser_Feed(RouterFrameParser *parser, uint8_t byte,
                              uint32_t now_ms, RouterFrame *out);
#endif
