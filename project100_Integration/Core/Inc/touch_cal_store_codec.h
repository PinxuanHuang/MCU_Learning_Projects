/* M2.4: on-flash record format and CRC32; pure C, no HAL dependencies.
 * 19 x uint32_t = 76 bytes; little-endian STM32F407, IEEE-754 binary64.
 */
#ifndef TOUCH_CAL_STORE_CODEC_H_
#define TOUCH_CAL_STORE_CODEC_H_

#include "touch_calibration.h"
#include <stdint.h>

#define TOUCH_CAL_STORE_RECORD_WORDS 19U
#define TOUCH_CAL_STORE_RECORD_BYTES (TOUCH_CAL_STORE_RECORD_WORDS * 4U)
#define TOUCH_CAL_STORE_MAGIC       0x4D324341UL /* 'M2CA' */
#define TOUCH_CAL_STORE_VERSION     1UL
#define TOUCH_CAL_STORE_GEOMETRY    ((3UL << 28U) | (TOUCH_CAL_WIDTH << 12U) | TOUCH_CAL_HEIGHT)
#define TOUCH_CAL_STORE_COMMIT      0xA61BC3D7UL

typedef struct {
    uint32_t words[TOUCH_CAL_STORE_RECORD_WORDS];
} TouchCalRecord;

/* The last word is the commit marker, programmed only after data and CRC.
 * Input MUST be the successful M2.2 calibration, never a fallback guess.
 */
uint8_t TouchCalRecord_Encode(TouchCalRecord *record,
                            const TouchCalibration *cal,
                            uint32_t sequence, uint16_t fit_error_px);
uint8_t TouchCalRecord_Decode(const TouchCalRecord *record,
                            TouchCalibration *cal,
                            uint32_t *sequence, uint16_t *fit_error_px);
uint8_t TouchCalRecord_IsBlank(const TouchCalRecord *record);
uint32_t TouchCalRecord_Crc32(const uint8_t *data, uint32_t size);

#endif /* TOUCH_CAL_STORE_CODEC_H_ */
