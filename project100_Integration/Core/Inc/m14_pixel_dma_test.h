/* M1.4.4: boot-only color pixel DMA and clipping verification. */
#ifndef M14_PIXEL_DMA_TEST_H_
#define M14_PIXEL_DMA_TEST_H_

#include <stdint.h>

typedef enum
{
    M14_PIXEL_DMA_TEST_PASS = 0U,
    M14_PIXEL_DMA_TEST_NOT_IDLE = 1U,
    M14_PIXEL_DMA_TEST_START = 2U,
    M14_PIXEL_DMA_TEST_BUSY = 3U,
    M14_PIXEL_DMA_TEST_WAIT = 4U,
    M14_PIXEL_DMA_TEST_CLIP_START = 5U,
    M14_PIXEL_DMA_TEST_CLIP_WAIT = 6U,
    M14_PIXEL_DMA_TEST_ARG = 7U,
    M14_PIXEL_DMA_TEST_FAULT = 8U
} M14_PixelDmaTestResult;

M14_PixelDmaTestResult M14_PixelDMA_Test_Run(void);

#endif /* M14_PIXEL_DMA_TEST_H_ */
