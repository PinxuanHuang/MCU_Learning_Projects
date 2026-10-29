/* Pure C tap recognizer: no SPI, HAL, IRQ or display dependencies. */
#include "touch_gesture.h"
#include <stddef.h>

void TouchGesture_Init(TouchGesture *gesture)
{
    if (gesture == NULL) return;
    *gesture = (TouchGesture){0};
}

uint8_t TouchGesture_Feed(TouchGesture *gesture, uint8_t pressed,
                        uint16_t x, uint16_t y, uint32_t now_ms,
                        TouchGestureTap *tap)
{
    if (gesture == NULL || tap == NULL) return 0U;
    gesture->samples++;

    if (pressed != 0U)
    {
        if (gesture->state == TOUCH_GESTURE_CANCELLED) return 0U;
        if (x >= TOUCH_WIDTH || y >= TOUCH_HEIGHT)
        {
            /* Reject this entire contact until release, not just one sample. */
            gesture->rejected_bounds++;
            gesture->state = TOUCH_GESTURE_CANCELLED;
            return 0U;
        }
        if (gesture->state == TOUCH_GESTURE_IDLE)
        {
            gesture->down_x = x;
            gesture->down_y = y;
            gesture->down_tick = now_ms;
            gesture->state = TOUCH_GESTURE_TRACKING;
            return 0U;
        }

        int32_t dx = (int32_t)x - (int32_t)gesture->down_x;
        int32_t dy = (int32_t)y - (int32_t)gesture->down_y;
        if (dx * dx + dy * dy >
            (int32_t)(TOUCH_MAX_MOVE_PX * TOUCH_MAX_MOVE_PX))
        {
            gesture->rejected_move++;
            gesture->state = TOUCH_GESTURE_CANCELLED;
        }
        else if ((uint32_t)(now_ms - gesture->down_tick) >
                 TOUCH_MAX_PRESS_MS)
        {
            gesture->rejected_long++;
            gesture->state = TOUCH_GESTURE_CANCELLED;
        }
        return 0U;
    }

    /* Some controllers have no meaningful coordinates once pressed == 0. */
    if (gesture->state != TOUCH_GESTURE_TRACKING)
    {
        gesture->state = TOUCH_GESTURE_IDLE;
        return 0U;
    }

    gesture->state = TOUCH_GESTURE_IDLE;
    uint32_t elapsed = (uint32_t)(now_ms - gesture->down_tick);
    if (elapsed < TOUCH_MIN_PRESS_MS)
    {
        gesture->rejected_short++;
        return 0U;
    }
    if (elapsed > TOUCH_MAX_PRESS_MS)
    {
        gesture->rejected_long++;
        return 0U;
    }

    tap->x = gesture->down_x;
    tap->y = gesture->down_y;
    tap->tick_ms = now_ms;
    gesture->taps++;
    return 1U;
}
