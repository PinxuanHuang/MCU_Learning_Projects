/* Pure C tap recognizer: no SPI, HAL, IRQ or display dependencies. */
#include "m2_touch_gesture.h"
#include <stddef.h>

void M2_Gesture_Init(M2_Gesture *gesture)
{
    if (gesture == NULL) return;
    *gesture = (M2_Gesture){0};
}

uint8_t M2_Gesture_Feed(M2_Gesture *gesture, uint8_t pressed,
                        uint16_t x, uint16_t y, uint32_t now_ms,
                        M2_GestureTap *tap)
{
    if (gesture == NULL || tap == NULL) return 0U;
    gesture->samples++;

    if (pressed != 0U)
    {
        if (gesture->state == M2_GESTURE_CANCELLED) return 0U;
        if (x >= M2_TOUCH_WIDTH || y >= M2_TOUCH_HEIGHT)
        {
            /* Reject this entire contact until release, not just one sample. */
            gesture->rejected_bounds++;
            gesture->state = M2_GESTURE_CANCELLED;
            return 0U;
        }
        if (gesture->state == M2_GESTURE_IDLE)
        {
            gesture->down_x = x;
            gesture->down_y = y;
            gesture->down_tick = now_ms;
            gesture->state = M2_GESTURE_TRACKING;
            return 0U;
        }

        int32_t dx = (int32_t)x - (int32_t)gesture->down_x;
        int32_t dy = (int32_t)y - (int32_t)gesture->down_y;
        if (dx * dx + dy * dy >
            (int32_t)(M2_TOUCH_MAX_MOVE_PX * M2_TOUCH_MAX_MOVE_PX))
        {
            gesture->rejected_move++;
            gesture->state = M2_GESTURE_CANCELLED;
        }
        else if ((uint32_t)(now_ms - gesture->down_tick) >
                 M2_TOUCH_MAX_PRESS_MS)
        {
            gesture->rejected_long++;
            gesture->state = M2_GESTURE_CANCELLED;
        }
        return 0U;
    }

    /* Some controllers have no meaningful coordinates once pressed == 0. */
    if (gesture->state != M2_GESTURE_TRACKING)
    {
        gesture->state = M2_GESTURE_IDLE;
        return 0U;
    }

    gesture->state = M2_GESTURE_IDLE;
    uint32_t elapsed = (uint32_t)(now_ms - gesture->down_tick);
    if (elapsed < M2_TOUCH_MIN_PRESS_MS)
    {
        gesture->rejected_short++;
        return 0U;
    }
    if (elapsed > M2_TOUCH_MAX_PRESS_MS)
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
