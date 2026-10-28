/* M3.1: Non-blocking Router Dashboard data-provider boundary.
 * Pure C, foreground-only. No hardware I/O, SPI, DMA, or Flash access.
 */
#ifndef M3_DATA_PROVIDER_H_
#define M3_DATA_PROVIDER_H_

#include <stdint.h>
#include "dashboard.h"
#include "m2_input.h"

typedef enum {
    M3_DATA_SOURCE_DEMO = 0U,
    M3_DATA_SOURCE_EXTERNAL = 1U
} M3_DataSource;

typedef struct {
    uint32_t demo_updates;
    uint32_t demo_touch_actions;
    uint32_t external_updates_accepted;
    uint32_t external_updates_rejected;
    uint32_t external_updates_coalesced;
    uint32_t external_stale_edges;
    uint32_t touch_ignored_external;
    uint32_t snapshots_emitted;
    uint32_t last_external_ms;
    uint8_t source;               /* M3_DataSource */
    uint8_t external_stale;       /* Diagnostic only: no automatic fallback */
} M3_DataProviderStats;

extern volatile M3_DataProviderStats m3_data_provider_stats;

/* Initializes the original demo statistics and copies them to initial_out. */
void M3_DataProvider_Init(uint32_t now_ms, DashboardStats *initial_out);

/* For the current demo only: apply a recognized Dashboard tap.
 * External telemetry is read-only; touches never falsify real measurements.
 * Returns 1 on a changed snapshot, else 0. */
uint8_t M3_DataProvider_OnTouch(M2_InputTarget target, DashboardStats *out);

/* Main-loop poll. DEMO emits on a 1000ms cadence; EXTERNAL emits only when a
 * freshly published value is pending. Returns 1 when *out has new data.
 * Independent of Dashboard DMA status; caller must enqueue via Dashboard_Update.
 */
uint8_t M3_DataProvider_Poll(uint32_t now_ms, DashboardStats *out);

/* M3.2 transport adapter should call from foreground context, never an ISR.
 * Valid range for CPU and RAM usage is 0..100. Latest external sample wins.
 * Explicitly switches to external mode. No transport is implemented in M3.1. */
uint8_t M3_DataProvider_PublishExternal(const DashboardStats *sample,
                                        uint32_t now_ms);

/* Explicit recovery to demo mode; does not erase calibration or telemetry. */
void M3_DataProvider_UseDemo(uint32_t now_ms, DashboardStats *out);

#endif /* M3_DATA_PROVIDER_H_ */
