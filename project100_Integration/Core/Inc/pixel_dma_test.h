/* M1.4.4: boot-only color pixel DMA and clipping verification. */
#ifndef PIXEL_DMA_TEST_H_
#define PIXEL_DMA_TEST_H_

#include <stdint.h>

typedef enum
{
    PIXEL_DMA_TEST_PASS = 0U,
    PIXEL_DMA_TEST_NOT_IDLE = 1U,
    PIXEL_DMA_TEST_START = 2U,
    PIXEL_DMA_TEST_BUSY = 3U,
    PIXEL_DMA_TEST_WAIT = 4U,
    PIXEL_DMA_TEST_CLIP_START = 5U,
    PIXEL_DMA_TEST_CLIP_WAIT = 6U,
    PIXEL_DMA_TEST_ARG = 7U,
    PIXEL_DMA_TEST_FAULT = 8U
} PixelDmaTestResult;

PixelDmaTestResult PixelDmaTest_Run(void);

#endif /* PIXEL_DMA_TEST_H_ */
