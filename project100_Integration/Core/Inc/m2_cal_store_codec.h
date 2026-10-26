/* M2.4: on-flash record format and CRC32; pure C, no HAL dependencies.
 * 19 x uint32_t = 76 bytes; little-endian STM32F407, IEEE-754 binary64.
 */
#ifndef M2_CAL_STORE_CODEC_H_
#define M2_CAL_STORE_CODEC_H_

#include "m2_calibration.h"
#include <stdint.h>

#define M2_CAL_STORE_RECORD_WORDS 19U
#define M2_CAL_STORE_RECORD_BYTES (M2_CAL_STORE_RECORD_WORDS * 4U)
#define M2_CAL_STORE_MAGIC       0x4D324341UL /* 'M2CA' */
#define M2_CAL_STORE_VERSION     1UL
#define M2_CAL_STORE_GEOMETRY    ((3UL << 28U) | (M2_CAL_WIDTH << 12U) | M2_CAL_HEIGHT)
#define M2_CAL_STORE_COMMIT      0xA61BC3D7UL

typedef struct {
    uint32_t words[M2_CAL_STORE_RECORD_WORDS];
} M2_CalRecord;

/* The last word is the commit marker, programmed only after data and CRC.
 * Input MUST be the successful M2.2 calibration, never a fallback guess.
 */
uint8_t M2_CalRecord_Encode(M2_CalRecord *record,
                            const M2_Calibration *cal,
                            uint32_t sequence, uint16_t fit_error_px);
uint8_t M2_CalRecord_Decode(const M2_CalRecord *record,
                            M2_Calibration *cal,
                            uint32_t *sequence, uint16_t *fit_error_px);
uint8_t M2_CalRecord_IsBlank(const M2_CalRecord *record);
uint32_t M2_CalRecord_Crc32(const uint8_t *data, uint32_t size);

#endif /* M2_CAL_STORE_CODEC_H_ */
