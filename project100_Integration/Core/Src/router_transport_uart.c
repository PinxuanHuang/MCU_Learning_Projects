#include "router_transport_uart.h"
#include "main.h"    /* STM32F4 HAL core, GPIO/RCC, and CMSIS USART registers */
#include <stddef.h>

/* No HAL UART module needed: USART2 RX is deliberately read at register level
 * in one IRQ, avoiding another DMA allocation or a byte-wise HAL rearm race.
 * On STM32F4 the read of SR followed by DR clears ORE/FE/NE/PE. */
volatile RouterUartStats router_uart_stats;
RouterRxPipeline router_rx_pipeline;
static uint32_t observed_uart_errors;

uint8_t RouterUart_Init(void)
{
    RouterRxPipeline_Init(&router_rx_pipeline);
    router_uart_stats = (RouterUartStats){0};
    observed_uart_errors = 0U;

    const uint32_t peripheral_hz = HAL_RCC_GetPCLK1Freq();
    /* STM32 USART BRR at oversampling 16 is PCLK / baud, rounded.
     * Permit a broad 9600..1,000,000 baud-compatible divider range. */
    const uint32_t divider = (peripheral_hz + ROUTER_UART_BAUD / 2U) /
                             ROUTER_UART_BAUD;
    if (divider < 16U || divider > UINT16_MAX) {
        router_uart_stats.bad_clock++;
        return 0U;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;            /* Idle high without cable */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    USART2->CR1 = 0U;
    USART2->CR2 = 0U;                   /* 1 stop bit */
    USART2->CR3 = 0U;                   /* No flow control or DMA */
    USART2->BRR = divider;              /* 115200 baud @ APB1 25 MHz */
    (void)USART2->SR;
    (void)USART2->DR;                   /* Clear pre-existing RX/error flags */

    /* Current .ioc uses NVIC_PRIORITYGROUP_0: all priority bits are
     * subpriority, so DMA (subpriority 0) precedes USART2 (2) when pending.
     * There is no IRQ preemption within this group. */
    HAL_NVIC_SetPriority(USART2_IRQn, 0U, 2U);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    router_uart_stats.enabled = 1U;
    USART2->CR3 = USART_CR3_EIE;
    USART2->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE;
    return 1U;
}

void RouterUart_IrqHandler(void)
{
    if (router_uart_stats.enabled == 0U) return;
    /* Capture status *before* DR; this order clears STM32F4 RX error bits. */
    const uint32_t status = USART2->SR;
    const uint32_t data = USART2->DR;
    router_uart_stats.irq_calls++;

    const uint32_t errors = status & (USART_SR_ORE | USART_SR_FE |
                                       USART_SR_NE | USART_SR_PE);
    if (errors != 0U) {
        if ((errors & USART_SR_ORE) != 0U) router_uart_stats.uart_overrun++;
        if ((errors & USART_SR_FE) != 0U) router_uart_stats.uart_framing++;
        if ((errors & USART_SR_NE) != 0U) router_uart_stats.uart_noise++;
        if ((errors & USART_SR_PE) != 0U) router_uart_stats.uart_parity++;
        /* The interrupted frame may have lost an unknown number of bytes.
         * Reset the stream in foreground before accepting future packets. */
        return;
    }
    if ((status & USART_SR_RXNE) == 0U) return;
    router_uart_stats.rx_bytes++;
    if (RouterRxPipeline_QueueByteFromIsr(&router_rx_pipeline, (uint8_t)data,
                                           HAL_GetTick()) == 0U)
        router_uart_stats.queue_full++;
}

uint8_t RouterUart_Poll(uint32_t now_ms, RouterRxSample *sample)
{
    if (sample == NULL || router_uart_stats.enabled == 0U) return 0U;

    /* Invalidate a truncated stream after UART receive errors. IRQ masking
     * keeps producer index stable while foreground discards the queue. */
    const uint32_t errors = router_uart_stats.uart_overrun +
                            router_uart_stats.uart_framing +
                            router_uart_stats.uart_noise +
                            router_uart_stats.uart_parity;
    if (errors != observed_uart_errors) {
        HAL_NVIC_DisableIRQ(USART2_IRQn);
        observed_uart_errors = router_uart_stats.uart_overrun +
                               router_uart_stats.uart_framing +
                               router_uart_stats.uart_noise +
                               router_uart_stats.uart_parity;
        const uint32_t head = __atomic_load_n(&router_rx_pipeline.producer,
                                               __ATOMIC_ACQUIRE);
        __atomic_store_n(&router_rx_pipeline.consumer, head, __ATOMIC_RELEASE);
        router_rx_pipeline.parser.used = 0U;
        router_rx_pipeline.parser.have_sequence = 0U;
        router_rx_pipeline.have_sequence = 0U;
        router_rx_pipeline.observed_dropped = __atomic_load_n(
            &router_rx_pipeline.bytes_dropped, __ATOMIC_ACQUIRE);
        router_uart_stats.receive_resets++;
        HAL_NVIC_EnableIRQ(USART2_IRQn);
        return 0U;
    }
    return RouterRxPipeline_Poll(&router_rx_pipeline, now_ms,
                                 ROUTER_RX_MAX_BYTES_PER_POLL, sample);
}
