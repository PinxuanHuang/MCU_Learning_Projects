/*
 * dashboard.c
 *
 *  Created on: Oct 12, 2026
 *      Author: pinxuanhuang
 */

#include <stdbool.h>
#include <stdio.h>
#include "dashboard.h"
#include "ili9341.h"

static DashboardStats last_stats;
static bool has_last_stats = false;

/* Foreground-only, single-display DMA bar update scheduler.
 * Text and static widgets stay on the blocking path in this milestone.
 */
static uint16_t cpu_bar_width = 0U;
static uint16_t mem_bar_width = 0U;
static uint16_t cpu_target_width = 0U;
static uint16_t mem_target_width = 0U;
static uint8_t active_bar = 0U; /* 0=none, 1=CPU, 2=RAM */
static uint8_t bar_pending = 0U; /* bit0=CPU, bit1=RAM */


#define BG       0x1082
#define PANEL    0x2104
#define FG       ILI9341_WHITE
#define CYAN     ILI9341_CYAN
#define GREEN    ILI9341_GREEN

static void draw_label(uint16_t x,
                       uint16_t y,
                       const char *text)
{
    ILI9341_DrawString(x, y, text, FG, BG, 1);
}

void Dashboard_Init(void)
{
    /* Must be called only while ILI9341 DMA is idle. */
    /* Never reset the screen while a DMA transfer is active. */
    if (ILI9341_DMA_IsBusy() || ILI9341_DMA_HasError()) return;
    has_last_stats = false;
    cpu_bar_width = mem_bar_width = 0U;
    cpu_target_width = mem_target_width = 0U;
    active_bar = bar_pending = 0U;
    ILI9341_FillScreen(BG);

    /* Title */
    ILI9341_DrawString(
        12, 10,
        "ROUTER DASHBOARD",
        CYAN, BG, 1);

    ILI9341_DrawHLine(8, 28, 304, FG);

    /* Static labels */
    draw_label(12, 42, "WAN : ONLINE");
    draw_label(12, 67, "RX  :");
    draw_label(12, 92, "TX  :");
    draw_label(12, 128, "CPU :");
    draw_label(12, 175, "RAM :");

    /* Progress bar backgrounds */
    ILI9341_FillRect(12, 148, 280, 12, PANEL);
    ILI9341_FillRect(12, 195, 280, 12, PANEL);
}

void Dashboard_Update(const DashboardStats *stats)
{
    if (stats == NULL || Dashboard_IsBusy() || ILI9341_DMA_HasError())
        return;
    /* Single foreground caller: finish all blocking text before DMA bars. */

    char buf[32];

    uint8_t cpu = stats->cpu_usage > 100U
                ? 100U : stats->cpu_usage;

    uint8_t mem = stats->mem_usage > 100U
                ? 100U : stats->mem_usage;

    uint8_t last_cpu = last_stats.cpu_usage > 100U
                     ? 100U : last_stats.cpu_usage;

    uint8_t last_mem = last_stats.mem_usage > 100U
                     ? 100U : last_stats.mem_usage;

    /* RX changed */
    if (!has_last_stats ||
        stats->rx_kbps != last_stats.rx_kbps)
    {
        ILI9341_FillRect(65, 67, 230, 12, BG);

        snprintf(buf, sizeof(buf),
                 "%lu kbps",
                 (unsigned long)stats->rx_kbps);

        ILI9341_DrawString(
            65, 67, buf, CYAN, BG, 1);
    }

    /* TX changed */
    if (!has_last_stats ||
        stats->tx_kbps != last_stats.tx_kbps)
    {
        ILI9341_FillRect(65, 92, 230, 12, BG);

        snprintf(buf, sizeof(buf),
                 "%lu kbps",
                 (unsigned long)stats->tx_kbps);

        ILI9341_DrawString(
            65, 92, buf, CYAN, BG, 1);
    }

    /* CPU changed */
    if (!has_last_stats || cpu != last_cpu)
    {
        ILI9341_FillRect(65, 128, 100, 12, BG);

        snprintf(buf, sizeof(buf), "%u%%",
                 (unsigned)cpu);

        ILI9341_DrawString(
            65, 128, buf, FG, BG, 1);

        cpu_target_width = (uint16_t)(280U * cpu / 100U);
        if (cpu_target_width != cpu_bar_width) bar_pending |= 1U;
    }

    /* RAM changed */
    if (!has_last_stats || mem != last_mem)
    {
        ILI9341_FillRect(65, 175, 100, 12, BG);

        snprintf(buf, sizeof(buf), "%u%%",
                 (unsigned)mem);

        ILI9341_DrawString(
            65, 175, buf, FG, BG, 1);

        mem_target_width = (uint16_t)(280U * mem / 100U);
        if (mem_target_width != mem_bar_width) bar_pending |= 2U;
    }

    last_stats = *stats;
    has_last_stats = true;
}

/* Returns true while either progress bar is queued or DMA is active.
 * Call Dashboard_Service() continuously from the foreground main loop.
 */
uint8_t Dashboard_IsBusy(void)
{
    return (uint8_t)(active_bar != 0U || bar_pending != 0U ||
                     ILI9341_DMA_IsBusy());
}

void Dashboard_Service(void)
{
    ILI9341_Status status;
    uint16_t from, to, y, color;
    uint8_t which;

    ILI9341_DMA_Service();
    if (ILI9341_DMA_HasError()) return; /* main handles fault */
    if (ILI9341_DMA_IsBusy()) return;

    /* DMA has completed; only now update the committed bar width. */
    if (active_bar == 1U)
        cpu_bar_width = cpu_target_width;
    else if (active_bar == 2U)
        mem_bar_width = mem_target_width;
    active_bar = 0U;

    if (bar_pending == 0U) return;
    which = (bar_pending & 1U) ? 1U : 2U;
    from = which == 1U ? cpu_bar_width : mem_bar_width;
    to   = which == 1U ? cpu_target_width : mem_target_width;
    y    = which == 1U ? 148U : 195U;
    if (from == to)
    {
        bar_pending &= (uint8_t)~(which == 1U ? 1U : 2U);
        return;
    }
    color = to > from ? (which == 1U ? CYAN : GREEN) : PANEL;
    status = ILI9341_FillRectDMA((uint16_t)(12U + (to > from ? from : to)),
                                  y,
                                  (uint16_t)(to > from ? to - from : from - to),
                                  12U, color);
    if (status == ILI9341_OK)
    {
        active_bar = which;
        bar_pending &= (uint8_t)~(which == 1U ? 1U : 2U);
    }
    /* On non-OK, keep the pending bit. DMA error is handled by main. */
}
