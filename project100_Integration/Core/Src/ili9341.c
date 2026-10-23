/*
 * ili9341.c
 *
 *  Created on: Oct 12, 2026
 *      Author: pinxuanhuang
 */

#include "ili9341.h"
#include <stddef.h>

/* STM32F407G-DISC1 + your confirmed wiring:
 * PB13 SPI2_SCK -> SCK; PB15 SPI2_MOSI -> SDI; PB14 -> SDO (optional)
 * PB12 -> CS, PD6 -> DC, PD7 -> RESET. TFT VCC/LED -> 3.3 V, GND -> GND.
 * For another board, change the port/pin macros below.
 */
extern SPI_HandleTypeDef hspi2;
#define TFT_SPI         (&hspi2)
#define TFT_CS_PORT     GPIOB
#define TFT_CS_PIN      GPIO_PIN_12
#define TFT_DC_PORT     GPIOD
#define TFT_DC_PIN      GPIO_PIN_6
#define TFT_RST_PORT    GPIOD
#define TFT_RST_PIN     GPIO_PIN_7

#define CMD_SWRESET 0x01U
#define CMD_SLPOUT  0x11U
#define CMD_DISPON  0x29U
#define CMD_CASET   0x2AU
#define CMD_PASET   0x2BU
#define CMD_RAMWR   0x2CU
#define CMD_MADCTL  0x36U
#define CMD_COLMOD  0x3AU

#define MADCTL_MY 0x80U
#define MADCTL_MX 0x40U
#define MADCTL_MV 0x20U

#define PIXEL_BATCH 256U

/* DMA Configuration */
#define ILI9341_DMA_TIMEOUT_MS 1000U
#define ILI9341_DMA_PIXELS 2048U

typedef enum
{
    TFT_DMA_IDLE = 0,
    TFT_DMA_ACTIVE,
    TFT_DMA_ERROR
} TFT_DMA_State;

/* Which payload should be staged into the DMA-accessible TX buffer. */
typedef enum
{
    TFT_DMA_SOURCE_SOLID = 0,
    TFT_DMA_SOURCE_PIXELS
} TFT_DMA_Source;

/* Must remain valid throughout DMA transfers */
static uint8_t dma_tx_buf[ILI9341_DMA_PIXELS * 2U];

static volatile TFT_DMA_State dma_state = TFT_DMA_IDLE;

static volatile uint8_t dma_chunk_done = 0;
static volatile uint8_t dma_error = 0;

static uint8_t dma_inflight = 0;

static uint32_t dma_remaining_pixels = 0;
static uint16_t dma_current_pixels = 0;

static uint32_t dma_start_tick = 0;

/* Pixel source is borrowed (not owned): caller keeps it stable until IDLE. */
static TFT_DMA_Source dma_source = TFT_DMA_SOURCE_SOLID;
static const uint16_t *dma_src_pixels = NULL;
static uint16_t dma_src_stride = 0U;
static uint16_t dma_visible_width = 0U;
static uint32_t dma_sent_pixels = 0U;

static uint16_t s_width = ILI9341_NATIVE_WIDTH;
static uint16_t s_height = ILI9341_NATIVE_HEIGHT;
static uint8_t s_initialized = 0;

static void cs_low(void) { HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET); }
static void cs_high(void) { HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET); }
static void dc_cmd(void) { HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_RESET); }
static void dc_data(void) { HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET); }

/* All TFT calls originate in the foreground; do not call any drawing API in ISR. */
static ILI9341_Status tft_ready(void)
{
    if (dma_state == TFT_DMA_ACTIVE) return ILI9341_BUSY;
    if (dma_state == TFT_DMA_ERROR || dma_error) return ILI9341_ERROR;
    return ILI9341_OK;
}

static ILI9341_Status tx(const uint8_t *buf, uint16_t len)
{
    if (tft_ready() != ILI9341_OK) return tft_ready();
    return HAL_SPI_Transmit(TFT_SPI, (uint8_t *)(uintptr_t)buf, len, 1000U) == HAL_OK
           ? ILI9341_OK : ILI9341_ERROR;
}
static ILI9341_Status cmd(uint8_t value)
{
    dc_cmd(); cs_low();
    ILI9341_Status s = tx(&value, 1);
    cs_high();
    return s;
}
static ILI9341_Status data(const uint8_t *buf, uint16_t len)
{
    dc_data(); cs_low();
    ILI9341_Status s = tx(buf, len);
    cs_high();
    return s;
}
static ILI9341_Status cmd1(uint8_t c, uint8_t p)
{
    ILI9341_Status s = cmd(c);
    if (s != ILI9341_OK) return s;
    return data(&p, 1);
}
static ILI9341_Status address_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t p[4];
    ILI9341_Status s;
    p[0] = (uint8_t)(x0 >> 8); p[1] = (uint8_t)x0;
    p[2] = (uint8_t)(x1 >> 8); p[3] = (uint8_t)x1;
    s = cmd(CMD_CASET); if (s != ILI9341_OK) return s;
    s = data(p, 4); if (s != ILI9341_OK) return s;
    p[0] = (uint8_t)(y0 >> 8); p[1] = (uint8_t)y0;
    p[2] = (uint8_t)(y1 >> 8); p[3] = (uint8_t)y1;
    s = cmd(CMD_PASET); if (s != ILI9341_OK) return s;
    s = data(p, 4); if (s != ILI9341_OK) return s;
    return cmd(CMD_RAMWR);
}

uint16_t ILI9341_Width(void) { return s_width; }
uint16_t ILI9341_Height(void) { return s_height; }

ILI9341_Status ILI9341_SetRotation(uint8_t rotation)
{
    if (tft_ready() != ILI9341_OK) return tft_ready();
    if (rotation > 3U) return ILI9341_BAD_ARG;
    /* Common ILI9341 geometry mapping; rotation 0 uses 0x48 (MX|BGR),
       preserving your known-working color/scan configuration. */
    static const uint8_t orientation[] = {
        MADCTL_MX,
        MADCTL_MV,
        MADCTL_MY,
        MADCTL_MX | MADCTL_MY | MADCTL_MV
    };
    const uint8_t v = orientation[rotation] | ILI9341_MADCTL_BGR;
    ILI9341_Status s = cmd1(CMD_MADCTL, v);
    if (s == ILI9341_OK) {
        s_width = (rotation & 1U) ? ILI9341_NATIVE_HEIGHT : ILI9341_NATIVE_WIDTH;
        s_height = (rotation & 1U) ? ILI9341_NATIVE_WIDTH : ILI9341_NATIVE_HEIGHT;
    }
    return s;
}

ILI9341_Status ILI9341_Init(void)
{
    if (tft_ready() != ILI9341_OK) return tft_ready();
    s_initialized = 0;
    cs_high();
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(150);
    ILI9341_Status s = cmd(CMD_SWRESET); if (s != ILI9341_OK) return s;
    HAL_Delay(150);
    s = cmd(CMD_SLPOUT); if (s != ILI9341_OK) return s;
    HAL_Delay(150);
    s = cmd1(CMD_COLMOD, 0x55U); if (s != ILI9341_OK) return s; /* 16-bit/pixel */
    s = ILI9341_SetRotation(0); if (s != ILI9341_OK) return s;
    s = cmd(CMD_DISPON); if (s != ILI9341_OK) return s;
    HAL_Delay(100);
    s_initialized = 1;
    return ILI9341_OK;
}

ILI9341_Status ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (tft_ready() != ILI9341_OK) return tft_ready();
    if (!s_initialized) return ILI9341_ERROR;
    if (w == 0U || h == 0U) return ILI9341_OK;
    if (x >= s_width || y >= s_height) return ILI9341_OK;
    /* Clip before adding: avoids integer overflow and x+w off-by-one bugs. */
    if (w > s_width - x) w = s_width - x;
    if (h > s_height - y) h = s_height - y;
    ILI9341_Status s = address_window(x, y, (uint16_t)(x+w-1U), (uint16_t)(y+h-1U));
    if (s != ILI9341_OK) return s;
    uint8_t buf[PIXEL_BATCH * 2U];
    for (size_t i = 0; i < PIXEL_BATCH; i++) {
        buf[i*2] = (uint8_t)(color >> 8);
        buf[i*2+1] = (uint8_t)color;
    }
    uint32_t remaining = (uint32_t)w * (uint32_t)h;
    dc_data(); cs_low();
    while (remaining) {
        uint16_t pixels = remaining > PIXEL_BATCH ? PIXEL_BATCH : (uint16_t)remaining;
        s = tx(buf, (uint16_t)(pixels * 2U));
        if (s != ILI9341_OK) break;
        remaining -= pixels;
    }
    cs_high();
    return s;
}
ILI9341_Status ILI9341_FillScreen(uint16_t color)
{
    return ILI9341_FillRect(0, 0, s_width, s_height, color);
}
ILI9341_Status ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    return ILI9341_FillRect(x, y, 1, 1, color);
}
ILI9341_Status ILI9341_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    return ILI9341_FillRect(x, y, w, 1, color);
}
ILI9341_Status ILI9341_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    return ILI9341_FillRect(x, y, 1, h, color);
}
ILI9341_Status ILI9341_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0U || h == 0U) return ILI9341_OK;
    ILI9341_Status s = ILI9341_DrawHLine(x,y,w,color); if (s != ILI9341_OK) return s;
    if (h > 1U) { s=ILI9341_DrawHLine(x,(uint16_t)(y+h-1U),w,color); if(s!=ILI9341_OK)return s; }
    s = ILI9341_DrawVLine(x,y,h,color); if (s != ILI9341_OK) return s;
    if (w > 1U) s=ILI9341_DrawVLine((uint16_t)(x+w-1U),y,h,color);
    return s;
}
ILI9341_Status ILI9341_DrawLine(int16_t x0,int16_t y0,int16_t x1,int16_t y1,uint16_t color)
{
    /* Bresenham algorithm with signed intermediate values. */
    int32_t x=x0, y=y0, a=x1, b=y1;
    int32_t dx=(x<a)?a-x:x-a, sx=(x<a)?1:-1;
    int32_t dy=-((y<b)?b-y:y-b), sy=(y<b)?1:-1;
    int32_t err=dx+dy;
    for (;;) {
        if (x>=0 && y>=0 && x<s_width && y<s_height) {
            ILI9341_Status s=ILI9341_DrawPixel((uint16_t)x,(uint16_t)y,color);
            if(s!=ILI9341_OK)return s;
        }
        if(x==a && y==b) break;
        int32_t e2=2*err;
        if(e2>=dy) {err+=dy; x+=sx;}
        if(e2<=dx) {err+=dx; y+=sy;}
    }
    return ILI9341_OK;
}

/* Public-domain 5x7 ASCII column font (ASCII 0x20..0x7E),
 * adapted from the classic glcdfont 5x7 bitmap layout. */
static const uint8_t font5x7[95][5] = {
{0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5f,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
{0x14,0x7f,0x14,0x7f,0x14}, {0x24,0x2a,0x7f,0x2a,0x12}, {0x23,0x13,0x08,0x64,0x62},
{0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1c,0x22,0x41,0x00},
{0x00,0x41,0x22,0x1c,0x00}, {0x14,0x08,0x3e,0x08,0x14}, {0x08,0x08,0x3e,0x08,0x08},
{0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
{0x20,0x10,0x08,0x04,0x02}, {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
{0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31}, {0x18,0x14,0x12,0x7f,0x10},
{0x27,0x45,0x45,0x45,0x39}, {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
{0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}, {0x00,0x36,0x36,0x00,0x00},
{0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
{0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3e},
{0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36}, {0x3e,0x41,0x41,0x41,0x22},
{0x7f,0x41,0x41,0x22,0x1c}, {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
{0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f}, {0x00,0x41,0x7f,0x41,0x00},
{0x20,0x40,0x41,0x3f,0x01}, {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
{0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f}, {0x3e,0x41,0x41,0x41,0x3e},
{0x7f,0x09,0x09,0x09,0x06}, {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
{0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01}, {0x3f,0x40,0x40,0x40,0x3f},
{0x1f,0x20,0x40,0x20,0x1f}, {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
{0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7f,0x41,0x41,0x00},
{0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7f,0x00}, {0x04,0x02,0x01,0x02,0x04},
{0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
{0x7f,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7f},
{0x38,0x54,0x54,0x54,0x18}, {0x08,0x7e,0x09,0x01,0x02}, {0x0c,0x52,0x52,0x52,0x3e},
{0x7f,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7d,0x40,0x00}, {0x20,0x40,0x44,0x3d,0x00},
{0x7f,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7f,0x40,0x00}, {0x7c,0x04,0x18,0x04,0x78},
{0x7c,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7c,0x14,0x14,0x14,0x08},
{0x08,0x14,0x14,0x18,0x7c}, {0x7c,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
{0x04,0x3f,0x44,0x40,0x20}, {0x3c,0x40,0x40,0x20,0x7c}, {0x1c,0x20,0x40,0x20,0x1c},
{0x3c,0x40,0x30,0x40,0x3c}, {0x44,0x28,0x10,0x28,0x44}, {0x0c,0x50,0x50,0x50,0x3c},
{0x44,0x64,0x54,0x4c,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7f,0x00,0x00},
{0x00,0x41,0x36,0x08,0x00}, {0x10,0x08,0x08,0x10,0x08}
};
/* Shared glyph accessor for the RAM tile renderer. No SPI transaction. */
uint8_t ILI9341_Font5x7Column(char ch, uint8_t column)
{
    unsigned char c = (unsigned char)ch;
    if (column >= 5U) return 0U;
    if (c < 32U || c > 126U) c = (unsigned char)'?';
    return font5x7[c - 32U][column];
}

ILI9341_Status ILI9341_DrawChar(uint16_t x,uint16_t y,char ch,uint16_t fg,uint16_t bg,uint8_t scale)
{
    if (tft_ready() != ILI9341_OK) return tft_ready();
    if(scale==0U)return ILI9341_BAD_ARG;
    unsigned char c=(unsigned char)ch;
    if(c<32U || c>126U)c='?';
    /* One 6x8 cell, including 1 column / row of background spacing. */
    for(uint8_t col=0;col<6U;col++) {
        uint8_t bits = (col<5U) ? font5x7[c-32U][col] : 0U;
        for(uint8_t row=0;row<8U;row++) {
            uint16_t color=(col<5U && row<7U && ((bits>>row)&1U)) ? fg : bg;
            uint32_t px=(uint32_t)x+(uint32_t)col*scale;
            uint32_t py=(uint32_t)y+(uint32_t)row*scale;
            if(px>=s_width || py>=s_height)continue;
            ILI9341_Status s=ILI9341_FillRect((uint16_t)px,(uint16_t)py,scale,scale,color);
            if(s!=ILI9341_OK)return s;
        }
    }
    return ILI9341_OK;
}
ILI9341_Status ILI9341_DrawString(uint16_t x,uint16_t y,const char *str,uint16_t fg,uint16_t bg,uint8_t scale)
{
    if (tft_ready() != ILI9341_OK) return tft_ready();
    if(!str || scale==0U)return ILI9341_BAD_ARG;
    uint32_t px=x,py=y;
    for(;*str;str++) {
        if(*str=='\n') {px=x;py+=(uint32_t)8U*scale;continue;}
        if(px+(uint32_t)6U*scale>s_width) {px=x;py+=(uint32_t)8U*scale;}
        if(py>=s_height)break;
        ILI9341_Status s=ILI9341_DrawChar((uint16_t)px,(uint16_t)py,*str,fg,bg,scale);
        if(s!=ILI9341_OK)return s;
        px+=(uint32_t)6U*scale;
    }
    return ILI9341_OK;
}


/*
 * The 4096-byte buffer is static in regular SRAM (not STM32F4 CCM RAM).
 * Only the foreground service starts transfers. The ISR merely posts flags.
 */
static ILI9341_Status dma_start_chunk(void)
{
    uint16_t pixels;
    HAL_StatusTypeDef ret;
    if (dma_remaining_pixels == 0U) return ILI9341_OK;
    pixels = (dma_remaining_pixels > ILI9341_DMA_PIXELS)
           ? ILI9341_DMA_PIXELS : (uint16_t)dma_remaining_pixels;
    dma_current_pixels = pixels;

    /* The ILI9341 expects RGB565 high-byte first. STM32 is little-endian,
     * so a direct uint16_t DMA would swap the bytes. Convert into the same
     * stable 4096-byte TX staging buffer used by FillRectDMA().
     */
    if (dma_source == TFT_DMA_SOURCE_PIXELS)
    {
        uint32_t row = dma_sent_pixels / dma_visible_width;
        uint32_t col = dma_sent_pixels % dma_visible_width;
        for (uint16_t i = 0U; i < pixels; ++i)
        {
            uint16_t color = dma_src_pixels[row * (uint32_t)dma_src_stride + col];
            dma_tx_buf[2U * i] = (uint8_t)(color >> 8);
            dma_tx_buf[2U * i + 1U] = (uint8_t)color;
            if (++col == dma_visible_width)
            {
                col = 0U;
                ++row;
            }
        }
    }

    dma_chunk_done = 0U;
    dma_inflight = 1U; /* Arm before start: callback may run immediately. */
    dma_start_tick = HAL_GetTick();
    ret = HAL_SPI_Transmit_DMA(TFT_SPI, dma_tx_buf, (uint16_t)(pixels * 2U));
    if (ret != HAL_OK)
    {
        dma_inflight = 0U;
        dma_error = 1U;
        return ILI9341_ERROR;
    }
    return ILI9341_OK;
}

static ILI9341_Status dma_stop_fault(void)
{
    /* Never unlock/release CS until HAL confirms SPI/DMA is stopped. */
    if (HAL_SPI_Abort(TFT_SPI) != HAL_OK)
    {
        dma_state = TFT_DMA_ERROR;
        return ILI9341_ERROR;
    }
    dma_inflight = 0U;
    dma_chunk_done = 0U;
    dma_current_pixels = 0U;
    dma_src_pixels = NULL;
    cs_high();
    dma_state = TFT_DMA_ERROR; /* Fault remains latched until Recovery. */
    return ILI9341_ERROR;
}

ILI9341_Status ILI9341_FillRectDMA(
    uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    ILI9341_Status s;
    if (tft_ready() != ILI9341_OK) return tft_ready();
    if (!s_initialized) return ILI9341_ERROR;
    if (w == 0U || h == 0U || x >= s_width || y >= s_height) return ILI9341_OK;
    if (w > s_width - x) w = s_width - x;
    if (h > s_height - y) h = s_height - y;

    for (uint32_t i = 0U; i < ILI9341_DMA_PIXELS; ++i)
    {
        dma_tx_buf[2U*i] = (uint8_t)(color >> 8);
        dma_tx_buf[2U*i+1U] = (uint8_t)color;
    }

    s = address_window(x, y, (uint16_t)(x+w-1U), (uint16_t)(y+h-1U));
    if (s != ILI9341_OK) return s;

    dma_source = TFT_DMA_SOURCE_SOLID;
    dma_src_pixels = NULL;
    dma_src_stride = 0U;
    dma_visible_width = 0U;
    dma_sent_pixels = 0U;
    dma_remaining_pixels = (uint32_t)w * h;
    dma_current_pixels = 0U;
    dma_chunk_done = 0U;
    dma_error = 0U;
    dma_inflight = 0U;
    dma_state = TFT_DMA_ACTIVE;
    dc_data();
    cs_low();
    if (dma_start_chunk() != ILI9341_OK)
    {
        (void)dma_stop_fault();
        return ILI9341_ERROR;
    }
    return ILI9341_OK; /* Started, not completed. */
}

ILI9341_Status ILI9341_FillScreenDMA(uint16_t color)
{
    return ILI9341_FillRectDMA(0U, 0U, s_width, s_height, color);
}

/* Asynchronous RGB565 rectangle blit.
 * Pixels: row-major uint16_t RGB565 with stride=original width.
 * Pixels must remain valid and unmodified until DMA_IsBusy() is false.
 * Call from foreground only; do not call simultaneously from ISR/RTOS tasks.
 * Right/bottom clipping preserves original input row stride.
 */
ILI9341_Status ILI9341_WriteRectDMA(uint16_t x, uint16_t y,
                                    uint16_t w, uint16_t h,
                                    const uint16_t *pixels)
{
    ILI9341_Status s = tft_ready();
    uint16_t original_width = w;
    if (s != ILI9341_OK) return s;
    if (!s_initialized) return ILI9341_ERROR;
    if (pixels == NULL) return ILI9341_BAD_ARG;
    if (w == 0U || h == 0U || x >= s_width || y >= s_height)
        return ILI9341_OK;
    if (w > s_width - x) w = s_width - x;
    if (h > s_height - y) h = s_height - y;

    s = address_window(x, y, (uint16_t)(x + w - 1U),
                       (uint16_t)(y + h - 1U));
    if (s != ILI9341_OK) return s;

    dma_source = TFT_DMA_SOURCE_PIXELS;
    dma_src_pixels = pixels;
    dma_src_stride = original_width;
    dma_visible_width = w;
    dma_sent_pixels = 0U;
    dma_remaining_pixels = (uint32_t)w * h;
    dma_current_pixels = 0U;
    dma_chunk_done = 0U;
    dma_error = 0U;
    dma_inflight = 0U;
    dma_state = TFT_DMA_ACTIVE;
    dc_data();
    cs_low();
    if (dma_start_chunk() != ILI9341_OK)
    {
        (void)dma_stop_fault();
        return ILI9341_ERROR;
    }
    return ILI9341_OK; /* Transaction started, not finished. */
}

void ILI9341_DMA_Service(void)
{
    if (dma_state != TFT_DMA_ACTIVE) return;
    if (dma_error)
    {
        (void)dma_stop_fault();
        return;
    }
    if (dma_inflight && dma_chunk_done)
    {
        dma_chunk_done = 0U;
        dma_inflight = 0U;
        dma_remaining_pixels -= dma_current_pixels;
        dma_sent_pixels += dma_current_pixels;
        dma_current_pixels = 0U;
    }
    if (dma_inflight)
    {
        if ((uint32_t)(HAL_GetTick() - dma_start_tick) > ILI9341_DMA_TIMEOUT_MS)
        {
            dma_error = 1U;
            (void)dma_stop_fault();
        }
        return;
    }
    if (dma_remaining_pixels == 0U)
    {
        cs_high();
        dma_src_pixels = NULL; /* Release borrowed image buffer. */
        dma_state = TFT_DMA_IDLE;
        return;
    }
    if (dma_start_chunk() != ILI9341_OK)
        (void)dma_stop_fault();
}

uint8_t ILI9341_DMA_IsBusy(void)
{
    return (uint8_t)(dma_state == TFT_DMA_ACTIVE);
}

uint8_t ILI9341_DMA_HasError(void)
{
    return (uint8_t)(dma_state == TFT_DMA_ERROR || dma_error != 0U);
}

ILI9341_Status ILI9341_DMA_Wait(uint32_t timeout_ms)
{
    uint32_t begin = HAL_GetTick();
    while (ILI9341_DMA_IsBusy())
    {
        ILI9341_DMA_Service();
        if ((uint32_t)(HAL_GetTick() - begin) > timeout_ms)
        {
            dma_error = 1U;
            (void)dma_stop_fault();
            return ILI9341_TIMEOUT;
        }
    }
    return ILI9341_DMA_HasError() ? ILI9341_ERROR : ILI9341_OK;
}

ILI9341_Status ILI9341_DMA_Recover(void)
{
    if (dma_state == TFT_DMA_ACTIVE) return ILI9341_BUSY;
    if (dma_state == TFT_DMA_IDLE && !dma_error) return ILI9341_OK;
    if (HAL_SPI_Abort(TFT_SPI) != HAL_OK) return ILI9341_ERROR;
    cs_high();
    dma_chunk_done = 0U;
    dma_error = 0U;
    dma_inflight = 0U;
    dma_remaining_pixels = 0U;
    dma_current_pixels = 0U;
    dma_sent_pixels = 0U;
    dma_src_pixels = NULL;
    dma_state = TFT_DMA_IDLE;
    return ILI9341_OK; /* Caller must redraw after interrupted transaction. */
}

void ILI9341_DMA_TxCompleteCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == TFT_SPI && dma_state == TFT_DMA_ACTIVE)
        dma_chunk_done = 1U;
}

void ILI9341_DMA_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == TFT_SPI && dma_state == TFT_DMA_ACTIVE)
        dma_error = 1U;
}
