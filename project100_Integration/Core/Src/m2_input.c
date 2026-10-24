/* M2.1: debounced button-generated virtual touch events.
 * No SPI transfers; existing ILI9341 DMA remains the sole SPI2 owner.
 * M2_Input_* must only be called from the foreground main loop.
 */
#include "m2_input.h"
#include "main.h"
#include <stddef.h>

#define M2_BUTTON_PIN GPIO_PIN_0
#define M2_BUTTON_DEBOUNCE_MS 25U
#define M2_EVENT_QUEUE_SIZE 8U

static M2_InputEvent s_queue[M2_EVENT_QUEUE_SIZE];
static uint8_t s_head, s_tail;
static uint8_t s_raw, s_stable, s_next_target;
static uint32_t s_change_tick;
volatile M2_InputStats m2_input_stats;

static void enqueue(uint16_t x, uint16_t y, uint32_t tick, M2_InputSource src)
{
    uint8_t next = (uint8_t)((s_head + 1U) % M2_EVENT_QUEUE_SIZE);
    if (next == s_tail)
    {
        m2_input_stats.events_dropped++;
        return; /* bounded queue: do not overwrite events still pending */
    }
    s_queue[s_head] = (M2_InputEvent){x, y, tick, src};
    s_head = next;
    m2_input_stats.events_queued++;
}

void M2_Input_Init(uint32_t now_ms)
{
    GPIO_InitTypeDef gpio = {0};
    /* UM1472: DISC1 B1 USER button drives PA0 high when pressed; R39
     * pulls it low when released. Does not use PB12/PB13/PB15/PD6/PD7. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = M2_BUTTON_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    s_head = s_tail = s_next_target = 0U;
    s_raw = (uint8_t)(HAL_GPIO_ReadPin(GPIOA, M2_BUTTON_PIN) == GPIO_PIN_SET);
    s_stable = s_raw;
    s_change_tick = now_ms;
    m2_input_stats = (M2_InputStats){0};
}

void M2_Input_Service(uint32_t now_ms)
{
    uint8_t raw = (uint8_t)(HAL_GPIO_ReadPin(GPIOA, M2_BUTTON_PIN) == GPIO_PIN_SET);
    if (raw != s_raw)
    {
        s_raw = raw;
        s_change_tick = now_ms;
    }
    if (s_raw != s_stable &&
        (uint32_t)(now_ms - s_change_tick) >= M2_BUTTON_DEBOUNCE_MS)
    {
        s_stable = s_raw;
        if (s_stable)
        {
            static const uint16_t x[4] = {90U, 90U, 80U, 80U};
            static const uint16_t y[4] = {73U, 97U, 153U, 200U};
            uint8_t n = s_next_target;
            s_next_target = (uint8_t)((n + 1U) % 4U);
            m2_input_stats.button_presses++;
            enqueue(x[n], y[n], now_ms, M2_INPUT_SOURCE_BUTTON_SIM);
        }
    }
}

uint8_t M2_Input_Pop(M2_InputEvent *out)
{
    if (out == NULL || s_head == s_tail) return 0U;
    *out = s_queue[s_tail];
    s_tail = (uint8_t)((s_tail + 1U) % M2_EVENT_QUEUE_SIZE);
    m2_input_stats.events_popped++;
    return 1U;
}

void M2_Input_InjectTouchTap(uint16_t x, uint16_t y, uint32_t now_ms)
{
    enqueue(x, y, now_ms, M2_INPUT_SOURCE_TOUCH);
}

M2_InputTarget M2_Input_Classify(const M2_InputEvent *event)
{
    uint16_t x, y;
    if (event == NULL) return M2_INPUT_TARGET_NONE;
    x = event->x;
    y = event->y;
    if (x >= 65U && x < 295U && y >= 67U && y < 79U)
        return M2_INPUT_TARGET_RX;
    if (x >= 65U && x < 295U && y >= 92U && y < 104U)
        return M2_INPUT_TARGET_TX;
    if (x >= 12U && x < 292U && y >= 128U && y < 160U)
        return M2_INPUT_TARGET_CPU;
    if (x >= 12U && x < 292U && y >= 175U && y < 207U)
        return M2_INPUT_TARGET_RAM;
    return M2_INPUT_TARGET_NONE;
}
