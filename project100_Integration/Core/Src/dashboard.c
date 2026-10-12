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
    if (stats == NULL)
        return;

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

        /* Repaint only the changed portion of the bar */
        uint16_t old_w =
            has_last_stats
            ? (uint16_t)(280U * last_cpu / 100U)
            : 0U;

        uint16_t new_w =
            (uint16_t)(280U * cpu / 100U);

        if (!has_last_stats)
            ILI9341_FillRect(12, 148, 280, 12, PANEL);

        if (new_w > old_w)
        {
            ILI9341_FillRect(
                12 + old_w, 148,
                new_w - old_w, 12, CYAN);
        }
        else if (new_w < old_w)
        {
            ILI9341_FillRect(
                12 + new_w, 148,
                old_w - new_w, 12, PANEL);
        }
    }

    /* RAM changed */
    if (!has_last_stats || mem != last_mem)
    {
        ILI9341_FillRect(65, 175, 100, 12, BG);

        snprintf(buf, sizeof(buf), "%u%%",
                 (unsigned)mem);

        ILI9341_DrawString(
            65, 175, buf, FG, BG, 1);

        uint16_t old_w =
            has_last_stats
            ? (uint16_t)(280U * last_mem / 100U)
            : 0U;

        uint16_t new_w =
            (uint16_t)(280U * mem / 100U);

        if (!has_last_stats)
            ILI9341_FillRect(12, 195, 280, 12, PANEL);

        if (new_w > old_w)
        {
            ILI9341_FillRect(
                12 + old_w, 195,
                new_w - old_w, 12, GREEN);
        }
        else if (new_w < old_w)
        {
            ILI9341_FillRect(
                12 + new_w, 195,
                old_w - new_w, 12, PANEL);
        }
    }

    last_stats = *stats;
    has_last_stats = true;
}
