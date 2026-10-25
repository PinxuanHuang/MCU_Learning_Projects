/* M2.1c: provisional ADS7846/XPT2046-family 12-bit raw touch sampler.
 * Does NOT modify the proven SPI2/ILI9341 DMA path.
 * STM32F407G-DISC1 P2 breakout: PC6 CLK, PC7 CS, PC8 DIN, PC9 DO, PC10 IRQ.
 * The U2 IC must be checked for XPT2046/ADS7846 compatibility.
 */
#include "m2_touch_raw.h"
#include "main.h"

#define TOUCH_PORT GPIOC
#define TOUCH_CLK  GPIO_PIN_6
#define TOUCH_CS   GPIO_PIN_7
#define TOUCH_DIN  GPIO_PIN_8
#define TOUCH_DO   GPIO_PIN_9
#define TOUCH_IRQ  GPIO_PIN_10

#define TOUCH_SAMPLE_PERIOD_MS 20U
#define TOUCH_SAMPLE_COUNT 5U
#define TOUCH_CMD_X 0xD0U
#define TOUCH_CMD_Y 0x90U

volatile M2_TouchRawStats m2_touch_raw_stats;
static uint32_t s_last_sample_ms;

static void pin_set(uint16_t pin, GPIO_PinState state)
{
    HAL_GPIO_WritePin(TOUCH_PORT, pin, state);
}

/* Software SPI mode 0, MSB first, deliberately slower than the LCD's SPI2.
 * HAL GPIO overhead bounds the clock speed; no hardware SPI peripheral shared.
 */
static uint8_t spi_xfer(uint8_t out)
{
    uint8_t in = 0U;
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        pin_set(TOUCH_DIN, (out & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        __NOP(); __NOP();
        pin_set(TOUCH_CLK, GPIO_PIN_SET);
        __NOP(); __NOP();
        in = (uint8_t)((in << 1U) |
              (HAL_GPIO_ReadPin(TOUCH_PORT, TOUCH_DO) == GPIO_PIN_SET ? 1U : 0U));
        pin_set(TOUCH_CLK, GPIO_PIN_RESET);
        out = (uint8_t)(out << 1U);
    }
    return in;
}

/* After the 8-bit command, capture the first 16 return clocks.
 * 12 meaningful ADC bits follow one leading null bit (>>3).
 * PD0=0 command values leave PENIRQ active between samples.
 */
static uint16_t read12(uint8_t command)
{
    pin_set(TOUCH_CS, GPIO_PIN_RESET);
    (void)spi_xfer(command);
    uint8_t hi = spi_xfer(0x00U);
    uint8_t lo = spi_xfer(0x00U);
    pin_set(TOUCH_CS, GPIO_PIN_SET);
    return (uint16_t)((((uint16_t)hi << 8U) | lo) >> 3U) & 0x0FFFU;
}

static uint16_t median5(uint16_t data[TOUCH_SAMPLE_COUNT])
{
    for (uint8_t i = 1U; i < TOUCH_SAMPLE_COUNT; ++i)
    {
        uint16_t key = data[i];
        uint8_t j = i;
        while (j > 0U && data[j - 1U] > key)
        {
            data[j] = data[j - 1U];
            --j;
        }
        data[j] = key;
    }
    return data[TOUCH_SAMPLE_COUNT / 2U];
}

void M2_TouchRaw_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Drive idle levels BEFORE configuring outputs, preventing a false select. */
    pin_set(TOUCH_CS, GPIO_PIN_SET);
    pin_set(TOUCH_CLK, GPIO_PIN_RESET);
    pin_set(TOUCH_DIN, GPIO_PIN_RESET);
    gpio.Pin = TOUCH_CLK | TOUCH_CS | TOUCH_DIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TOUCH_PORT, &gpio);

    gpio.Pin = TOUCH_DO;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(TOUCH_PORT, &gpio);

    gpio.Pin = TOUCH_IRQ;
    gpio.Pull = GPIO_PULLUP;   /* PENIRQ is typically active low / open drain. */
    HAL_GPIO_Init(TOUCH_PORT, &gpio);

    m2_touch_raw_stats = (M2_TouchRawStats){0};
    m2_touch_raw_stats.min_x = 4095U;
    m2_touch_raw_stats.min_y = 4095U;
    /* MCU reset need not reset the external controller. Complete a PD0=0
     * conversion before waiting for PENIRQ, so pen detection is enabled.
     * This startup conversion is discarded rather than counted as a touch.
     */
    (void)read12(TOUCH_CMD_Y);
    HAL_Delay(1U);
    m2_touch_raw_stats.initialized = 1U;
    s_last_sample_ms = HAL_GetTick() - TOUCH_SAMPLE_PERIOD_MS;
}

void M2_TouchRaw_Service(uint32_t now_ms)
{
    if (!m2_touch_raw_stats.initialized ||
        (uint32_t)(now_ms - s_last_sample_ms) < TOUCH_SAMPLE_PERIOD_MS)
        return;
    s_last_sample_ms = now_ms;
    m2_touch_raw_stats.polls++;

    /* Sample PENIRQ only when CS is high; during ADC conversions it may float. */
    uint8_t pressed = (uint8_t)(HAL_GPIO_ReadPin(TOUCH_PORT, TOUCH_IRQ)
                                     == GPIO_PIN_RESET);
    if (pressed != m2_touch_raw_stats.pressed)
    {
        if (pressed) m2_touch_raw_stats.pen_down_edges++;
        else         m2_touch_raw_stats.pen_up_edges++;
        m2_touch_raw_stats.pressed = pressed;
    }
    if (!pressed) return;

    uint16_t xs[TOUCH_SAMPLE_COUNT], ys[TOUCH_SAMPLE_COUNT];
    for (uint8_t i = 0U; i < TOUCH_SAMPLE_COUNT; ++i)
    {
        xs[i] = read12(TOUCH_CMD_X);
        ys[i] = read12(TOUCH_CMD_Y);
    }
    uint16_t x = median5(xs);
    uint16_t y = median5(ys);
    /* No fabricated screen coordinates; reject obvious absent/invalid ADC samples. */
    if (x == 0U || x == 4095U || y == 0U || y == 4095U)
    {
        m2_touch_raw_stats.invalid_samples++;
        return;
    }
    m2_touch_raw_stats.raw_x = x;
    m2_touch_raw_stats.raw_y = y;
    if (x < m2_touch_raw_stats.min_x) m2_touch_raw_stats.min_x = x;
    if (x > m2_touch_raw_stats.max_x) m2_touch_raw_stats.max_x = x;
    if (y < m2_touch_raw_stats.min_y) m2_touch_raw_stats.min_y = y;
    if (y > m2_touch_raw_stats.max_y) m2_touch_raw_stats.max_y = y;
    m2_touch_raw_stats.samples_ok++;
}
