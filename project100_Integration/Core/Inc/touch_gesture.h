/* M2.1b: hardware-independent touch contact -> TAP recognizer.
 * Coordinates must be calibrated to the 320x240 landscape display first.
 * Foreground-only; do not call from DMA ISR or concurrent RTOS threads.
 */
#ifndef TOUCH_GESTURE_H_
#define TOUCH_GESTURE_H_

#include <stdint.h>

#define TOUCH_WIDTH           320U
#define TOUCH_HEIGHT          240U
#define TOUCH_MIN_PRESS_MS     25U
#define TOUCH_MAX_PRESS_MS    600U
#define TOUCH_MAX_MOVE_PX      12U

typedef enum {
    TOUCH_GESTURE_IDLE = 0,
    TOUCH_GESTURE_TRACKING,
    TOUCH_GESTURE_CANCELLED
} TouchGestureState;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint32_t tick_ms;
} TouchGestureTap;

typedef struct {
    TouchGestureState state;
    uint16_t down_x;
    uint16_t down_y;
    uint32_t down_tick;
    uint32_t samples;
    uint32_t taps;
    uint32_t rejected_short;
    uint32_t rejected_long;
    uint32_t rejected_move;
    uint32_t rejected_bounds;
} TouchGesture;

void TouchGesture_Init(TouchGesture *gesture);
/* Return 1 ONLY on valid contact release and fill *tap.
 * x/y on release are ignored, since some controllers stop reporting them.
 */
uint8_t TouchGesture_Feed(TouchGesture *gesture, uint8_t pressed,
                        uint16_t x, uint16_t y, uint32_t now_ms,
                        TouchGestureTap *tap);

#endif /* TOUCH_GESTURE_H_ */
