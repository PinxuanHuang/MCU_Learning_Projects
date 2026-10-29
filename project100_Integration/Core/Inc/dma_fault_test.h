/*
 * dma_fault_test.h - M1.4.3 controlled SPI2 DMA fault-injection tests.
 * Only invoke from the foreground after ILI9341_Init / SetRotation.
 * Do not call from IRQ or while Dashboard_Service is updating the display.
 */
#ifndef DMA_FAULT_TEST_H_
#define DMA_FAULT_TEST_H_

#include "ili9341.h"
#include <stdint.h>

/* Debugger result: 0 means all selected checks passed. */
typedef enum
{
    DMA_TEST_PASS = 0U,
    DMA_TEST_NOT_IDLE = 1U,
    DMA_TEST_TIMEOUT_START = 2U,
    DMA_TEST_TIMEOUT_RESULT = 3U,
    DMA_TEST_TIMEOUT_FAULT_NOT_LATCHED = 4U,
    DMA_TEST_TIMEOUT_RECOVER = 5U,
    DMA_TEST_ERROR_START = 6U,
    DMA_TEST_ERROR_FAULT_NOT_LATCHED = 7U,
    DMA_TEST_ERROR_RECOVER = 8U,
    DMA_TEST_POST_RECOVERY_START = 9U,
    DMA_TEST_POST_RECOVERY_WAIT = 10U,
    DMA_TEST_POST_RECOVERY_STATE = 11U
} DmaFaultTestResult;

/* A software-injected SPI error event tests the driver recovery path,
 * not the electrical SPI bus or a real DMA transfer-error interrupt.
 */
DmaFaultTestResult DmaFaultTest_Run(SPI_HandleTypeDef *spi);

#endif /* DMA_FAULT_TEST_H_ */
