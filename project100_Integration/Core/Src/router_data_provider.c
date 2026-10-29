/* M3.1 - isolated DashboardStats producer with a demo and external adapter API.
 * No HAL dependencies; suitable for deterministic host-side tests.
 */
#include "router_data_provider.h"
#include <stddef.h>

#define ROUTER_DEMO_PERIOD_MS 1000U
#define ROUTER_EXTERNAL_STALE_MS 5000U

volatile RouterDataProviderStats router_data_provider_stats;
static DashboardStats s_snapshot;
static uint32_t s_last_demo_ms;
static uint8_t s_external_pending;

static void reset_demo(uint32_t now_ms)
{
    s_snapshot = (DashboardStats){1200U, 500U, 20U, 40U};
    s_last_demo_ms = now_ms;
    s_external_pending = 0U;
    router_data_provider_stats.source = (uint8_t)ROUTER_DATA_SOURCE_DEMO;
    router_data_provider_stats.external_stale = 0U;
}

void RouterDataProvider_Init(uint32_t now_ms, DashboardStats *initial_out)
{
    router_data_provider_stats = (RouterDataProviderStats){0};
    reset_demo(now_ms);
    if (initial_out != NULL) *initial_out = s_snapshot;
}

void RouterDataProvider_UseDemo(uint32_t now_ms, DashboardStats *out)
{
    reset_demo(now_ms);
    if (out != NULL) *out = s_snapshot;
}

uint8_t RouterDataProvider_OnTouch(TouchInputTarget target, DashboardStats *out)
{
    if (out == NULL || target == TOUCH_INPUT_TARGET_NONE) return 0U;
    if (router_data_provider_stats.source != (uint8_t)ROUTER_DATA_SOURCE_DEMO)
    {
        router_data_provider_stats.touch_ignored_external++;
        return 0U;
    }
    switch (target)
    {
        case TOUCH_INPUT_TARGET_RX:
            s_snapshot.rx_kbps += 1000U; break;
        case TOUCH_INPUT_TARGET_TX:
            s_snapshot.tx_kbps += 500U; break;
        case TOUCH_INPUT_TARGET_CPU:
            s_snapshot.cpu_usage =
                (uint8_t)(((uint32_t)s_snapshot.cpu_usage + 25U) % 101U);
            break;
        case TOUCH_INPUT_TARGET_RAM:
            s_snapshot.mem_usage =
                (uint8_t)(((uint32_t)s_snapshot.mem_usage + 25U) % 101U);
            break;
        default: return 0U;
    }
    router_data_provider_stats.demo_touch_actions++;
    *out = s_snapshot;
    router_data_provider_stats.snapshots_emitted++;
    return 1U;
}

uint8_t RouterDataProvider_PublishExternal(const DashboardStats *sample,
                                        uint32_t now_ms)
{
    if (sample == NULL || sample->cpu_usage > 100U ||
        sample->mem_usage > 100U)
    {
        router_data_provider_stats.external_updates_rejected++;
        return 0U;
    }
    if (s_external_pending != 0U)
        router_data_provider_stats.external_updates_coalesced++;
    s_snapshot = *sample; /* bounded struct copy; no external pointer retained */
    s_external_pending = 1U;
    router_data_provider_stats.source = (uint8_t)ROUTER_DATA_SOURCE_EXTERNAL;
    router_data_provider_stats.last_external_ms = now_ms;
    router_data_provider_stats.external_stale = 0U;
    router_data_provider_stats.external_updates_accepted++;
    return 1U;
}

uint8_t RouterDataProvider_Poll(uint32_t now_ms, DashboardStats *out)
{
    if (out == NULL) return 0U;
    if (router_data_provider_stats.source == (uint8_t)ROUTER_DATA_SOURCE_EXTERNAL)
    {
        if ((uint32_t)(now_ms - router_data_provider_stats.last_external_ms) >
            ROUTER_EXTERNAL_STALE_MS && router_data_provider_stats.external_stale == 0U)
        {
            router_data_provider_stats.external_stale = 1U;
            router_data_provider_stats.external_stale_edges++;
        }
        if (s_external_pending == 0U) return 0U;
        s_external_pending = 0U;
    }
    else
    {
        if ((uint32_t)(now_ms - s_last_demo_ms) < ROUTER_DEMO_PERIOD_MS)
            return 0U;
        /* Preserve the old main.c cadence: no catch-up burst after a pause. */
        s_last_demo_ms = now_ms;
        s_snapshot.rx_kbps += 100U;
        s_snapshot.tx_kbps += 50U;
        if (s_snapshot.rx_kbps > 5000U) s_snapshot.rx_kbps = 1000U;
        if (s_snapshot.tx_kbps > 2000U) s_snapshot.tx_kbps = 300U;
        s_snapshot.cpu_usage = (uint8_t)((s_snapshot.cpu_usage + 5U) % 100U);
        s_snapshot.mem_usage = (uint8_t)((s_snapshot.mem_usage + 3U) % 100U);
        router_data_provider_stats.demo_updates++;
    }
    *out = s_snapshot;
    router_data_provider_stats.snapshots_emitted++;
    return 1U;
}
