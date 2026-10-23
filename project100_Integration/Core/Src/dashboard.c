/*
 * dashboard.c - M1.4.5: foreground-only, non-blocking dashboard updates.
 *
 * Dashboard_Init() paints the static layout using the known-working blocking
 * API. Afterwards all dynamic fields are updated through SPI TX DMA.
 * Dashboard_Update() stores the latest requested statistics; it never draws.
 * Dashboard_Service() owns the SPI transaction scheduler.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include "dashboard.h"
#include "ili9341.h"

#define BG               0x1082U
#define PANEL            0x2104U
#define FG               ILI9341_WHITE
#define CYAN             ILI9341_CYAN
#define GREEN            ILI9341_GREEN
#define BAR_MAX_W        280U
#define TILE_MAX_W       230U
#define TILE_H           12U

/* One stable, row-major RGB565 source tile. The ILI9341 driver borrows it
 * for the full DMA transaction, including any 2048-pixel chunk boundaries.
 * Do NOT touch s_tile while s_active_job != JOB_NONE.
 */
static uint16_t s_tile[TILE_MAX_W * TILE_H];

typedef enum
{
    JOB_NONE = 0,
    JOB_RX,
    JOB_TX,
    JOB_CPU_TEXT,
    JOB_RAM_TEXT,
    JOB_CPU_BAR,
    JOB_RAM_BAR
} DashboardJob;

#define COMMITTED_BIT(job) (1U << ((uint32_t)(job) - 1U))

static DashboardStats s_desired;
static DashboardStats s_presented;
static DashboardStats s_inflight;
static bool s_has_desired = false;
static uint8_t s_committed_mask = 0U;
static DashboardJob s_active_job = JOB_NONE;
static uint16_t s_cpu_bar_width = 0U;
static uint16_t s_ram_bar_width = 0U;
static uint16_t s_inflight_bar_width = 0U;

/* Read-only access to the exact same 5x7 font used by blocking drawing. */
static void rasterize_text(const char *text, uint16_t width, uint16_t fg)
{
    for (uint32_t i = 0U; i < (uint32_t)width * TILE_H; ++i)
        s_tile[i] = BG;

    for (uint16_t ch = 0U; text[ch] != '\0'; ++ch)
    {
        uint32_t origin = (uint32_t)ch * 6U;
        if (origin >= width) break;
        for (uint8_t col = 0U; col < 5U; ++col)
        {
            uint32_t pixel_x = origin + col;
            if (pixel_x >= width) break;
            uint8_t bits = ILI9341_Font5x7Column(text[ch], col);
            for (uint8_t row = 0U; row < 7U; ++row)
            {
                if ((bits & (uint8_t)(1U << row)) != 0U)
                    s_tile[(uint32_t)row * width + pixel_x] = fg;
            }
        }
    }
}

static uint8_t is_committed(DashboardJob job)
{
    return (uint8_t)((s_committed_mask & COMMITTED_BIT(job)) != 0U);
}

/* Dirty region selection: recheck the desired snapshot after every completed
 * DMA transaction. This coalesces updates received while DMA was in flight.
 */
static DashboardJob next_dirty_job(void)
{
    if (!s_has_desired) return JOB_NONE;
    if (!is_committed(JOB_RX) || s_presented.rx_kbps != s_desired.rx_kbps)
        return JOB_RX;
    if (!is_committed(JOB_TX) || s_presented.tx_kbps != s_desired.tx_kbps)
        return JOB_TX;
    if (!is_committed(JOB_CPU_TEXT) || s_presented.cpu_usage != s_desired.cpu_usage)
        return JOB_CPU_TEXT;
    if (!is_committed(JOB_RAM_TEXT) || s_presented.mem_usage != s_desired.mem_usage)
        return JOB_RAM_TEXT;
    if (!is_committed(JOB_CPU_BAR) ||
        s_cpu_bar_width != (uint16_t)((uint32_t)s_desired.cpu_usage * BAR_MAX_W / 100U))
        return JOB_CPU_BAR;
    if (!is_committed(JOB_RAM_BAR) ||
        s_ram_bar_width != (uint16_t)((uint32_t)s_desired.mem_usage * BAR_MAX_W / 100U))
        return JOB_RAM_BAR;
    return JOB_NONE;
}

static void commit_completed_job(void)
{
    switch (s_active_job)
    {
        case JOB_RX:       s_presented.rx_kbps = s_inflight.rx_kbps; break;
        case JOB_TX:       s_presented.tx_kbps = s_inflight.tx_kbps; break;
        case JOB_CPU_TEXT: s_presented.cpu_usage = s_inflight.cpu_usage; break;
        case JOB_RAM_TEXT: s_presented.mem_usage = s_inflight.mem_usage; break;
        case JOB_CPU_BAR:  s_cpu_bar_width = s_inflight_bar_width; break;
        case JOB_RAM_BAR:  s_ram_bar_width = s_inflight_bar_width; break;
        default: return;
    }
    s_committed_mask |= (uint8_t)COMMITTED_BIT(s_active_job);
    s_active_job = JOB_NONE;
}

static ILI9341_Status start_job(DashboardJob job)
{
    char text[32];
    uint16_t x = 65U, y = 0U, width = 0U, fg = FG;
    if (job == JOB_RX || job == JOB_TX ||
        job == JOB_CPU_TEXT || job == JOB_RAM_TEXT)
    {
        switch (job)
        {
            case JOB_RX:
                y = 67U; width = 230U; fg = CYAN;
                (void)snprintf(text, sizeof(text), "%lu kbps",
                               (unsigned long)s_desired.rx_kbps);
                break;
            case JOB_TX:
                y = 92U; width = 230U; fg = CYAN;
                (void)snprintf(text, sizeof(text), "%lu kbps",
                               (unsigned long)s_desired.tx_kbps);
                break;
            case JOB_CPU_TEXT:
                y = 128U; width = 100U;
                (void)snprintf(text, sizeof(text), "%u%%",
                               (unsigned)s_desired.cpu_usage);
                break;
            default:
                y = 175U; width = 100U;
                (void)snprintf(text, sizeof(text), "%u%%",
                               (unsigned)s_desired.mem_usage);
                break;
        }
        rasterize_text(text, width, fg);
        return ILI9341_WriteRectDMA(x, y, width, TILE_H, s_tile);
    }

    /* Retain the M1.4.2 bandwidth-efficient delta bar updates. */
    if (job == JOB_CPU_BAR || job == JOB_RAM_BAR)
    {
        uint16_t before = job == JOB_CPU_BAR ? s_cpu_bar_width : s_ram_bar_width;
        uint16_t after = job == JOB_CPU_BAR
            ? (uint16_t)((uint32_t)s_desired.cpu_usage * BAR_MAX_W / 100U)
            : (uint16_t)((uint32_t)s_desired.mem_usage * BAR_MAX_W / 100U);
        s_inflight_bar_width = after;
        if (before == after) return ILI9341_OK; /* Already blank / unchanged. */
        uint16_t left = before < after ? before : after;
        uint16_t span = before < after ? after - before : before - after;
        uint16_t color = before < after
            ? (job == JOB_CPU_BAR ? CYAN : GREEN) : PANEL;
        y = job == JOB_CPU_BAR ? 148U : 195U;
        return ILI9341_FillRectDMA((uint16_t)(12U + left), y,
                                    span, TILE_H, color);
    }
    return ILI9341_BAD_ARG;
}

static void draw_label(uint16_t x, uint16_t y, const char *label)
{
    (void)ILI9341_DrawString(x, y, label, FG, BG, 1U);
}

void Dashboard_Init(void)
{
    /* Main-loop only; initialization intentionally uses blocking SPI. */
    if (ILI9341_DMA_IsBusy() || ILI9341_DMA_HasError()) return;
    s_has_desired = false;
    s_committed_mask = 0U;
    s_active_job = JOB_NONE;
    s_cpu_bar_width = 0U;
    s_ram_bar_width = 0U;
    s_inflight_bar_width = 0U;
    s_desired = (DashboardStats){0};
    s_presented = (DashboardStats){0};
    s_inflight = (DashboardStats){0};

    (void)ILI9341_FillScreen(BG);
    (void)ILI9341_DrawString(12U, 10U, "ROUTER DASHBOARD", CYAN, BG, 1U);
    (void)ILI9341_DrawHLine(8U, 28U, 304U, FG);
    draw_label(12U, 42U, "WAN : ONLINE");
    draw_label(12U, 67U, "RX  :");
    draw_label(12U, 92U, "TX  :");
    draw_label(12U, 128U, "CPU :");
    draw_label(12U, 175U, "RAM :");
    (void)ILI9341_FillRect(12U, 148U, BAR_MAX_W, TILE_H, PANEL);
    (void)ILI9341_FillRect(12U, 195U, BAR_MAX_W, TILE_H, PANEL);
}

void Dashboard_Update(const DashboardStats *stats)
{
    if (stats == NULL) return;
    /* Snapshot the value, never borrow caller-owned storage. Latest wins. */
    s_desired = *stats;
    if (s_desired.cpu_usage > 100U) s_desired.cpu_usage = 100U;
    if (s_desired.mem_usage > 100U) s_desired.mem_usage = 100U;
    s_has_desired = true;
}

uint8_t Dashboard_IsBusy(void)
{
    return (uint8_t)(s_active_job != JOB_NONE || ILI9341_DMA_IsBusy() ||
                     next_dirty_job() != JOB_NONE);
}

void Dashboard_Service(void)
{
    ILI9341_DMA_Service();
    if (ILI9341_DMA_HasError()) return; /* Existing main.c fault monitor. */
    if (ILI9341_DMA_IsBusy()) return;

    /* Driver has reached IDLE and no longer borrows s_tile. */
    if (s_active_job != JOB_NONE) commit_completed_job();

    /* One new transaction per iteration; foreground-only single owner. */
    for (uint8_t attempt = 0U; attempt < 6U; ++attempt)
    {
        DashboardJob job = next_dirty_job();
        if (job == JOB_NONE) return;
        s_inflight = s_desired;
        ILI9341_Status st = start_job(job);
        if (st != ILI9341_OK) return; /* Preserve dirty state. */

        /* Zero-width bar needs no transaction; mark it as presented. */
        if (!ILI9341_DMA_IsBusy())
        {
            s_active_job = job;
            commit_completed_job();
            continue;
        }
        s_active_job = job;
        return;
    }
}
