/* M1.4.4: optional foreground-only WriteRectDMA smoke test.
 * Keep compiled test disabled in normal firmware. Uses a static source
 * buffer so every asynchronous chunk can safely read it.
 */
#include "pixel_dma_test.h"
#include "ili9341.h"
#include "main.h"
#include <stddef.h>

#define TEST_W 80U
#define TEST_H 36U

static uint16_t test_pixels[TEST_W * TEST_H];

PixelDmaTestResult PixelDmaTest_Run(void)
{
    static const uint16_t colors[4] = {
        ILI9341_RED, ILI9341_GREEN, ILI9341_BLUE, ILI9341_YELLOW
    };
    if (ILI9341_DMA_IsBusy() || ILI9341_DMA_HasError())
        return PIXEL_DMA_TEST_NOT_IDLE;

    for (uint32_t y = 0U; y < TEST_H; ++y)
    {
        for (uint32_t x = 0U; x < TEST_W; ++x)
        {
            test_pixels[y * TEST_W + x] = colors[((x / 4U) + (y / 4U)) & 3U];
        }
    }

    if (ILI9341_WriteRectDMA(30U, 40U, TEST_W, TEST_H, test_pixels) != ILI9341_OK)
        return PIXEL_DMA_TEST_START;

    /* Both old blocking drawing and another pixel DMA must be rejected. */
    if (ILI9341_FillRect(0U, 0U, 1U, 1U, ILI9341_WHITE) != ILI9341_BUSY ||
        ILI9341_WriteRectDMA(0U, 0U, TEST_W, TEST_H, test_pixels) != ILI9341_BUSY)
        return PIXEL_DMA_TEST_BUSY;

    if (ILI9341_DMA_Wait(1000U) != ILI9341_OK)
        return PIXEL_DMA_TEST_WAIT;
    HAL_Delay(700U);

    /* Clip 80x36 input at 320x240 right/bottom edge to 13x21 output.
     * The original source stride must remain 80, not the clipped 13.
     */
    if (ILI9341_WriteRectDMA(307U, 219U, TEST_W, TEST_H, test_pixels) != ILI9341_OK)
        return PIXEL_DMA_TEST_CLIP_START;
    if (ILI9341_DMA_Wait(1000U) != ILI9341_OK)
        return PIXEL_DMA_TEST_CLIP_WAIT;
    HAL_Delay(700U);

    if (ILI9341_WriteRectDMA(0U, 0U, 1U, 1U, NULL) != ILI9341_BAD_ARG)
        return PIXEL_DMA_TEST_ARG;
    if (ILI9341_DMA_HasError() || ILI9341_DMA_IsBusy())
        return PIXEL_DMA_TEST_FAULT;
    return PIXEL_DMA_TEST_PASS;
}
