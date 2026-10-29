/*
 * M1.4.3: controlled fault injection for an existing SPI2 DMA driver.
 * Boot-only integration test; keep disabled in production firmware.
 * A failed test returns a specific code without silently clearing it.
 * The interrupted test images are redrawn by Dashboard_Init() after PASS.
 */
#include "dma_fault_test.h"
#include <stddef.h>

/* The 320x240 RGB565 transfer needs ~98 ms at SPI2 12.5 MHz,
 * therefore a 1 ms deadline intentionally causes a timeout.
 * Test requires SysTick / HAL_GetTick to advance normally.
 */
#define TIMEOUT_INJECT_MS 1U
#define HEALTHY_WAIT_MS 1500U

DmaFaultTestResult DmaFaultTest_Run(SPI_HandleTypeDef *spi)
{
    ILI9341_Status status;

    if (spi == NULL || ILI9341_DMA_IsBusy() || ILI9341_DMA_HasError())
        return DMA_TEST_NOT_IDLE;

    /* Test A: exercise the real HAL SPI Abort path via DMA_Wait timeout. */
    status = ILI9341_FillScreenDMA(ILI9341_RED);
    if (status != ILI9341_OK)
        return DMA_TEST_TIMEOUT_START;

    status = ILI9341_DMA_Wait(TIMEOUT_INJECT_MS);
    if (status != ILI9341_TIMEOUT)
        return DMA_TEST_TIMEOUT_RESULT;

    if (!ILI9341_DMA_HasError() || ILI9341_DMA_IsBusy())
        return DMA_TEST_TIMEOUT_FAULT_NOT_LATCHED;

    status = ILI9341_DMA_Recover();
    if (status != ILI9341_OK || ILI9341_DMA_HasError() || ILI9341_DMA_IsBusy())
        return DMA_TEST_TIMEOUT_RECOVER;

    /* Test B: simulate the error-notification path of the HAL callback.
     * A real DMA error IRQ is NOT injected here.  The driver must abort
     * the still-active real DMA transfer in DMA_Service().
     */
    status = ILI9341_FillScreenDMA(ILI9341_GREEN);
    if (status != ILI9341_OK)
        return DMA_TEST_ERROR_START;

    ILI9341_DMA_ErrorCallback(spi);
    ILI9341_DMA_Service();

    if (!ILI9341_DMA_HasError() || ILI9341_DMA_IsBusy())
        return DMA_TEST_ERROR_FAULT_NOT_LATCHED;

    status = ILI9341_DMA_Recover();
    if (status != ILI9341_OK || ILI9341_DMA_HasError() || ILI9341_DMA_IsBusy())
        return DMA_TEST_ERROR_RECOVER;

    /* Test C: after two abort/recover cycles the next real DMA transfer
     * must still be able to start and finish without a latched fault.
     */
    status = ILI9341_FillScreenDMA(ILI9341_BLUE);
    if (status != ILI9341_OK)
        return DMA_TEST_POST_RECOVERY_START;

    status = ILI9341_DMA_Wait(HEALTHY_WAIT_MS);
    if (status != ILI9341_OK)
        return DMA_TEST_POST_RECOVERY_WAIT;

    if (ILI9341_DMA_HasError() || ILI9341_DMA_IsBusy())
        return DMA_TEST_POST_RECOVERY_STATE;

    return DMA_TEST_PASS;
}
