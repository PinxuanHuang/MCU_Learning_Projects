/*
 * m14_dma_fault_test.h - M1.4.3 controlled SPI2 DMA fault-injection tests.
 * Only invoke from the foreground after ILI9341_Init / SetRotation.
 * Do not call from IRQ or while Dashboard_Service is updating the display.
 */
#ifndef M14_DMA_FAULT_TEST_H_
#define M14_DMA_FAULT_TEST_H_

#include "ili9341.h"
#include <stdint.h>

/* Debugger result: 0 means all selected checks passed. */
typedef enum
{
    M14_DMA_TEST_PASS = 0U,
    M14_DMA_TEST_NOT_IDLE = 1U,
    M14_DMA_TEST_TIMEOUT_START = 2U,
    M14_DMA_TEST_TIMEOUT_RESULT = 3U,
    M14_DMA_TEST_TIMEOUT_FAULT_NOT_LATCHED = 4U,
    M14_DMA_TEST_TIMEOUT_RECOVER = 5U,
    M14_DMA_TEST_ERROR_START = 6U,
    M14_DMA_TEST_ERROR_FAULT_NOT_LATCHED = 7U,
    M14_DMA_TEST_ERROR_RECOVER = 8U,
    M14_DMA_TEST_POST_RECOVERY_START = 9U,
    M14_DMA_TEST_POST_RECOVERY_WAIT = 10U,
    M14_DMA_TEST_POST_RECOVERY_STATE = 11U
} M14_DmaFaultTestResult;

/* A software-injected SPI error event tests the driver recovery path,
 * not the electrical SPI bus or a real DMA transfer-error interrupt.
 */
M14_DmaFaultTestResult M14_DMA_FaultTest_Run(SPI_HandleTypeDef *spi);

#endif /* M14_DMA_FAULT_TEST_H_ */
