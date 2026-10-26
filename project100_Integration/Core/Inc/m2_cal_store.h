/* M2.4 STM32F407VG calibration Flash journal.
 * Only sector 11 is used: 0x080E0000..0x080FFFFF (128 KiB).
 * The linker MUST reserve this sector: application FLASH length = 896K.
 */
#ifndef M2_CAL_STORE_H_
#define M2_CAL_STORE_H_

#include "m2_calibration.h"
#include <stdint.h>

#define M2_CAL_STORE_START_ADDR  0x080E0000UL
#define M2_CAL_STORE_SECTOR_SIZE 131072UL
#define M2_CAL_STORE_SLOT_COUNT  1724UL /* floor(128KiB / 76) */

typedef enum {
    M2_STORE_NONE = 0,
    M2_STORE_LOADED = 1,
    M2_STORE_EMPTY = 2,
    M2_STORE_SAVED = 3,
    M2_STORE_FULL = 4,
    M2_STORE_INVALID = 5,
    M2_STORE_WRITE_ERROR = 6
} M2_CalStoreResult;

typedef struct {
    uint32_t slots_used;
    uint32_t valid_records;
    uint32_t invalid_records;
    uint32_t last_sequence;
    uint32_t save_attempts;
    uint32_t save_success;
    uint32_t write_failures;
    uint16_t saved_fit_error_px;
    uint16_t loaded_fit_error_px;
    uint8_t loaded_from_flash;
    uint8_t force_recalibration;
    uint8_t result;
} M2_CalStoreStats;

extern volatile M2_CalStoreStats m2_cal_store_stats;

/* Call before display DMA begins, after clock and touch GPIO initialization. */
void M2_CalStore_Init(void);
uint8_t M2_CalStore_Load(M2_Calibration *cal);
uint8_t M2_CalStore_Save(const M2_Calibration *cal, uint16_t fit_error_px);
/* Press and hold DISC1 B1 before reset; ~1.2s at boot triggers re-calibration.
 * No data is erased just by holding the button.
 */
uint8_t M2_CalStore_ForceRecalibration(void);

#endif /* M2_CAL_STORE_H_ */
