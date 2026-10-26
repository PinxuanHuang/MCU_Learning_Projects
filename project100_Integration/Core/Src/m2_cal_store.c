/* M2.4: append-only calibration journal on STM32F407VG FLASH sector 11.
 * A record is visible only after its final commit word is programmed.
 * No sector erase during normal save; a full journal refuses new writes.
 * Any incomplete record is skipped at next boot; older valid record survives.
 * IMPORTANT: linker FLASH length MUST equal 896K, reserving sector 11.
 */
#include "m2_cal_store.h"
#include "m2_cal_store_codec.h"
#include "main.h"
#include <stddef.h>

_Static_assert(M2_CAL_STORE_SLOT_COUNT * M2_CAL_STORE_RECORD_BYTES <=
               M2_CAL_STORE_SECTOR_SIZE, "Flash journal out of bounds");

volatile M2_CalStoreStats m2_cal_store_stats;

static const volatile uint32_t *const s_flash =
    (const volatile uint32_t *)(uintptr_t)M2_CAL_STORE_START_ADDR;

static void read_slot(uint32_t slot, M2_CalRecord *out)
{
    const volatile uint32_t *src =
        s_flash + slot * M2_CAL_STORE_RECORD_WORDS;
    for (uint32_t i = 0U; i < M2_CAL_STORE_RECORD_WORDS; ++i)
        out->words[i] = src[i];
}

typedef struct {
    uint32_t next_slot;
    uint32_t sequence;
    uint16_t fit_error;
    uint8_t has_valid;
    M2_Calibration latest;
} ScanResult;

static void scan_journal(ScanResult *out)
{
    *out = (ScanResult){0};
    m2_cal_store_stats.valid_records = 0U;
    m2_cal_store_stats.invalid_records = 0U;
    M2_CalRecord rec;
    for (uint32_t slot = 0U; slot < M2_CAL_STORE_SLOT_COUNT; ++slot) {
        read_slot(slot, &rec);
        if (M2_CalRecord_IsBlank(&rec)) continue;
        /* Only append past the last used slot (even if it was interrupted). */
        out->next_slot = slot + 1U;
        M2_Calibration candidate;
        uint32_t seq;
        uint16_t fit;
        if (M2_CalRecord_Decode(&rec, &candidate, &seq, &fit)) {
            out->latest = candidate;
            out->sequence = seq;
            out->fit_error = fit;
            out->has_valid = 1U;
            m2_cal_store_stats.valid_records++;
        } else {
            m2_cal_store_stats.invalid_records++;
        }
    }
    m2_cal_store_stats.slots_used = out->next_slot;
    m2_cal_store_stats.last_sequence = out->sequence;
}

void M2_CalStore_Init(void)
{
    m2_cal_store_stats = (M2_CalStoreStats){0};
}

uint8_t M2_CalStore_Load(M2_Calibration *cal)
{
    if (cal == NULL) {
        m2_cal_store_stats.result = M2_STORE_INVALID;
        return 0U;
    }
    ScanResult scan;
    scan_journal(&scan);
    if (!scan.has_valid) {
        m2_cal_store_stats.result = M2_STORE_EMPTY;
        return 0U;
    }
    *cal = scan.latest;
    m2_cal_store_stats.loaded_from_flash = 1U;
    m2_cal_store_stats.loaded_fit_error_px = scan.fit_error;
    m2_cal_store_stats.result = M2_STORE_LOADED;
    return 1U;
}

uint8_t M2_CalStore_Save(const M2_Calibration *cal, uint16_t fit_error_px)
{
    m2_cal_store_stats.save_attempts++;
    ScanResult scan;
    scan_journal(&scan);
    if (scan.next_slot >= M2_CAL_STORE_SLOT_COUNT ||
        scan.sequence == 0xFFFFFFFFUL) {
        m2_cal_store_stats.result = M2_STORE_FULL;
        return 0U;
    }
    M2_CalRecord rec;
    if (!M2_CalRecord_Encode(&rec, cal, scan.sequence + 1U, fit_error_px)) {
        m2_cal_store_stats.result = M2_STORE_INVALID;
        return 0U;
    }
    uint32_t address = M2_CAL_STORE_START_ADDR +
                       scan.next_slot * M2_CAL_STORE_RECORD_BYTES;
    if (HAL_FLASH_Unlock() != HAL_OK) {
        m2_cal_store_stats.write_failures++;
        m2_cal_store_stats.result = M2_STORE_WRITE_ERROR;
        return 0U;
    }
    HAL_StatusTypeDef status = HAL_OK;
    /* Commit marker is the final write.  Incomplete writes cannot be loaded. */
    for (uint32_t i = 0U; i < M2_CAL_STORE_RECORD_WORDS; ++i) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                   address + i * 4U, rec.words[i]);
        if (status != HAL_OK) break;
    }
    (void)HAL_FLASH_Lock();
    M2_CalRecord verify;
    read_slot(scan.next_slot, &verify);
    M2_Calibration decoded;
    uint32_t seq;
    uint16_t fit;
    if (status != HAL_OK || !M2_CalRecord_Decode(&verify, &decoded, &seq, &fit) ||
        seq != scan.sequence + 1U || fit != fit_error_px) {
        m2_cal_store_stats.write_failures++;
        m2_cal_store_stats.result = M2_STORE_WRITE_ERROR;
        return 0U;
    }
    m2_cal_store_stats.save_success++;
    m2_cal_store_stats.last_sequence = seq;
    m2_cal_store_stats.saved_fit_error_px = fit;
    m2_cal_store_stats.slots_used = scan.next_slot + 1U;
    m2_cal_store_stats.result = M2_STORE_SAVED;
    return 1U;
}

uint8_t M2_CalStore_ForceRecalibration(void)
{
    /* On STM32F407-DISC1 the B1 USER button is PA0, active high. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) != GPIO_PIN_SET)
        return 0U; /* Fast boot when B1 was not held before reset. */

    /* DWT bounds the hold interval even if uwTick later stalls. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
        return 0U; /* Avoid waiting if trace cycle counter is unavailable. */
    uint32_t start_cycles = DWT->CYCCNT;
    uint32_t cycles_required = SystemCoreClock + SystemCoreClock / 5U;
    if (cycles_required == 0U || cycles_required > 120000000U)
        cycles_required = 60000000U;
    while ((uint32_t)(DWT->CYCCNT - start_cycles) < cycles_required) {
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) != GPIO_PIN_SET)
            return 0U;
    }
    m2_cal_store_stats.force_recalibration = 1U;
    return 1U;
}
