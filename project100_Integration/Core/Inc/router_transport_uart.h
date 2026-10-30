#ifndef ROUTER_TRANSPORT_UART_H_
#define ROUTER_TRANSPORT_UART_H_

#include <stdint.h>
#include "router_rx_pipeline.h"

/* STM32F407G-DISC1, USART2 RX on PA3 (P1 pin 13), 115200 8N1.
 * RX-only 3.3V TTL. IRQ is a single producer, main loop is the consumer.
 * No SPI2 DMA, LCD, touch, or flash dependencies.
 */
#define ROUTER_UART_BAUD 115200U

typedef struct {
    uint32_t irq_calls;
    uint32_t rx_bytes;
    uint32_t queue_full;
    uint32_t uart_overrun;
    uint32_t uart_framing;
    uint32_t uart_noise;
    uint32_t uart_parity;
    uint32_t receive_resets;
    uint32_t bad_clock;
    uint8_t enabled;
} RouterUartStats;

/* Exposed for CubeIDE Expressions. Single ISR writer for error counters,
 * single foreground writer for receive_resets. Do not reset while enabled. */
extern volatile RouterUartStats router_uart_stats;
extern RouterRxPipeline router_rx_pipeline;

/* Call only once after HAL_Init/SystemClock_Config and before the while loop.
 * GPIOA PA3/USART2/NVIC are configured here without CubeMX regeneration.
 * Returns 0 and leaves UART disabled if clock cannot support 115200 baud. */
uint8_t RouterUart_Init(void);

/* Call only from USART2_IRQHandler; no blocking work. */
void RouterUart_IrqHandler(void);

/* Foreground only, bounded to 96 received bytes per call. Returns one latest
 * valid sample; the caller publishes it through RouterDataProvider in main.
 */
uint8_t RouterUart_Poll(uint32_t now_ms, RouterRxSample *sample);

#endif /* ROUTER_TRANSPORT_UART_H_ */
