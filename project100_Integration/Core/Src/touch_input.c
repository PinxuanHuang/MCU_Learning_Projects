/* M2.1: debounced button-generated virtual touch events.
 * No SPI transfers; existing ILI9341 DMA remains the sole SPI2 owner.
 * TouchInput_* must only be called from the foreground main loop.
 */
#include "touch_input.h"
#include "touch_gesture.h"
#include "main.h"
#include <stddef.h>

#define TOUCH_BUTTON_PIN GPIO_PIN_0
#define TOUCH_BUTTON_DEBOUNCE_MS 25U
#define TOUCH_EVENT_QUEUE_SIZE 8U

static TouchInputEvent s_queue[TOUCH_EVENT_QUEUE_SIZE];
static uint8_t s_head, s_tail;
static uint8_t s_raw, s_stable, s_next_target;
static uint32_t s_change_tick;
/* Inspect s_gesture in CubeIDE Expressions while stopped in touch_input.c. */
static TouchGesture s_gesture;
volatile TouchInputStats touch_input_stats;

static uint8_t enqueue(uint16_t x, uint16_t y, uint32_t tick, TouchInputSource src)
{
    uint8_t next = (uint8_t)((s_head + 1U) % TOUCH_EVENT_QUEUE_SIZE);
    if (next == s_tail)
    {
        touch_input_stats.events_dropped++;
        return 0U; /* bounded queue: do not overwrite events still pending */
    }
    s_queue[s_head] = (TouchInputEvent){x, y, tick, src};
    s_head = next;
    touch_input_stats.events_queued++;
    return 1U;
}

void TouchInput_Init(uint32_t now_ms)
{
    GPIO_InitTypeDef gpio = {0};
    /* UM1472: DISC1 B1 USER button drives PA0 high when pressed; R39
     * pulls it low when released. Does not use PB12/PB13/PB15/PD6/PD7. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = TOUCH_BUTTON_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    s_head = s_tail = s_next_target = 0U;
    s_raw = (uint8_t)(HAL_GPIO_ReadPin(GPIOA, TOUCH_BUTTON_PIN) == GPIO_PIN_SET);
    s_stable = s_raw;
    s_change_tick = now_ms;
    touch_input_stats = (TouchInputStats){0};
    TouchGesture_Init(&s_gesture);
}

/* Hardware touch backend supplies calibrated screen coordinates plus
 * contact state. Events are created only when a valid tap is released.
 * Source buffer and SPI arbitration remain the hardware backend's job.
 */
uint8_t TouchInput_FeedTouchSample(uint8_t pressed, uint16_t x,
                                 uint16_t y, uint32_t now_ms)
{
    TouchGestureTap tap;
    if (!TouchGesture_Feed(&s_gesture, pressed, x, y, now_ms, &tap))
        return 0U;
    return enqueue(tap.x, tap.y, tap.tick_ms, TOUCH_INPUT_SOURCE_TOUCH);
}

void TouchInput_Service(uint32_t now_ms)
{
    uint8_t raw = (uint8_t)(HAL_GPIO_ReadPin(GPIOA, TOUCH_BUTTON_PIN) == GPIO_PIN_SET);
    if (raw != s_raw)
    {
        s_raw = raw;
        s_change_tick = now_ms;
    }
    if (s_raw != s_stable &&
        (uint32_t)(now_ms - s_change_tick) >= TOUCH_BUTTON_DEBOUNCE_MS)
    {
        s_stable = s_raw;
        if (s_stable)
        {
            static const uint16_t x[4] = {90U, 90U, 80U, 80U};
            static const uint16_t y[4] = {73U, 97U, 153U, 200U};
            uint8_t n = s_next_target;
            touch_input_stats.button_presses++;
            /* A stable press starts a contact; no TAP has fired yet. */
            (void)TouchGesture_Feed(&s_gesture, 1U, x[n], y[n], now_ms,
                                  &(TouchGestureTap){0});
        }
        else
        {
            /* A stable release completes a valid virtual touch TAP. */
            TouchGestureTap tap;
            if (TouchGesture_Feed(&s_gesture, 0U, 0U, 0U, now_ms, &tap))
            {
                if (enqueue(tap.x, tap.y, tap.tick_ms,
                            TOUCH_INPUT_SOURCE_BUTTON_SIM))
                    s_next_target = (uint8_t)((s_next_target + 1U) % 4U);
            }
        }
    }
}

uint8_t TouchInput_Pop(TouchInputEvent *out)
{
    if (out == NULL || s_head == s_tail) return 0U;
    *out = s_queue[s_tail];
    s_tail = (uint8_t)((s_tail + 1U) % TOUCH_EVENT_QUEUE_SIZE);
    touch_input_stats.events_popped++;
    return 1U;
}

void TouchInput_InjectTouchTap(uint16_t x, uint16_t y, uint32_t now_ms)
{
    (void)enqueue(x, y, now_ms, TOUCH_INPUT_SOURCE_TOUCH);
}

TouchInputTarget TouchInput_Classify(const TouchInputEvent *event)
{
    uint16_t x, y;
    if (event == NULL) return TOUCH_INPUT_TARGET_NONE;
    x = event->x;
    y = event->y;
    if (x >= 65U && x < 295U && y >= 67U && y < 79U)
        return TOUCH_INPUT_TARGET_RX;
    if (x >= 65U && x < 295U && y >= 92U && y < 104U)
        return TOUCH_INPUT_TARGET_TX;
    if (x >= 12U && x < 292U && y >= 128U && y < 160U)
        return TOUCH_INPUT_TARGET_CPU;
    if (x >= 12U && x < 292U && y >= 175U && y < 207U)
        return TOUCH_INPUT_TARGET_RAM;
    return TOUCH_INPUT_TARGET_NONE;
}
