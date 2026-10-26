/* M2.4: CRC-guarded affine coefficient encoding. No I/O side effects. */
#include "m2_cal_store_codec.h"
#include <string.h>

_Static_assert(sizeof(double) == 8U, "M2.4 needs IEEE-754 binary64 double");
_Static_assert(sizeof(M2_CalRecord) == 76U, "unexpected flash record layout");

uint32_t M2_CalRecord_Crc32(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    if (data == NULL) return 0U;
    for (uint32_t i = 0U; i < size; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* Conservative bounds and a non-singular transform. Prevent corrupted
 * coefficients that pass format checks from reaching float-to-int conversion.
 */
static uint8_t sane(const double coeff[6])
{
    for (uint8_t i = 0U; i < 6U; ++i) {
        /* Comparisons reject NaN and infinities without libm. */
        if (!(coeff[i] >= -4096.0 && coeff[i] <= 4096.0)) return 0U;
    }
    double det = coeff[0] * coeff[4] - coeff[1] * coeff[3];
    if (det > -0.00000001 && det < 0.00000001) return 0U;
    return 1U;
}

uint8_t M2_CalRecord_Encode(M2_CalRecord *record,
                            const M2_Calibration *cal,
                            uint32_t sequence, uint16_t fit_error_px)
{
    if (record == NULL || cal == NULL || !cal->valid ||
        fit_error_px > 18U || sequence == 0U) return 0U;
    const double coeff[6] = {cal->a, cal->b, cal->c,
                             cal->d, cal->e, cal->f};
    if (!sane(coeff)) return 0U;
    memset(record, 0xFF, sizeof(*record));
    record->words[0] = M2_CAL_STORE_MAGIC;
    record->words[1] = M2_CAL_STORE_VERSION;
    record->words[2] = sequence;
    record->words[3] = M2_CAL_STORE_GEOMETRY;
    record->words[4] = fit_error_px;
    memcpy(&record->words[5], coeff, sizeof(coeff));
    record->words[17] = M2_CalRecord_Crc32((const uint8_t *)record,
                                          17U * sizeof(uint32_t));
    record->words[18] = M2_CAL_STORE_COMMIT;
    return 1U;
}

uint8_t M2_CalRecord_Decode(const M2_CalRecord *record,
                            M2_Calibration *cal,
                            uint32_t *sequence, uint16_t *fit_error_px)
{
    if (record == NULL || cal == NULL) return 0U;
    if (record->words[0] != M2_CAL_STORE_MAGIC ||
        record->words[1] != M2_CAL_STORE_VERSION ||
        record->words[2] == 0U ||
        record->words[3] != M2_CAL_STORE_GEOMETRY ||
        record->words[4] > 18U ||
        record->words[18] != M2_CAL_STORE_COMMIT ||
        record->words[17] != M2_CalRecord_Crc32((const uint8_t *)record,
                                                17U * sizeof(uint32_t)))
        return 0U;
    double coeff[6];
    memcpy(coeff, &record->words[5], sizeof(coeff));
    if (!sane(coeff)) return 0U;
    *cal = (M2_Calibration){coeff[0], coeff[1], coeff[2],
                            coeff[3], coeff[4], coeff[5], 1U};
    if (sequence != NULL) *sequence = record->words[2];
    if (fit_error_px != NULL) *fit_error_px = (uint16_t)record->words[4];
    return 1U;
}

uint8_t M2_CalRecord_IsBlank(const M2_CalRecord *record)
{
    if (record == NULL) return 0U;
    for (uint8_t i = 0U; i < M2_CAL_STORE_RECORD_WORDS; ++i)
        if (record->words[i] != 0xFFFFFFFFUL) return 0U;
    return 1U;
}
