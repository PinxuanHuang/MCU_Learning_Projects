/*
 * ili9341.h
 *
 *  Created on: Oct 12, 2026
 *      Author: pinxuanhuang
 */

#ifndef INC_ILI9341_H_
#define INC_ILI9341_H_

#include "main.h"
#include <stdint.h>

/* RGB565 colors */
#define ILI9341_BLACK       0x0000U
#define ILI9341_WHITE       0xFFFFU
#define ILI9341_RED         0xF800U
#define ILI9341_GREEN       0x07E0U
#define ILI9341_BLUE        0x001FU
#define ILI9341_YELLOW      0xFFE0U
#define ILI9341_CYAN        0x07FFU
#define ILI9341_MAGENTA     0xF81FU
#define ILI9341_ORANGE      0xFD20U
#define ILI9341_GRAY        0x8410U

#define ILI9341_NATIVE_WIDTH   240U
#define ILI9341_NATIVE_HEIGHT  320U

/* The board's display worked with the MADCTL BGR bit set. */
#ifndef ILI9341_MADCTL_BGR
#define ILI9341_MADCTL_BGR 0x08U
#endif

typedef enum {
    ILI9341_OK = 0,
    ILI9341_ERROR,
    ILI9341_BAD_ARG,
    ILI9341_BUSY,
    ILI9341_TIMEOUT
} ILI9341_Status;

ILI9341_Status ILI9341_Init(void);
ILI9341_Status ILI9341_SetRotation(uint8_t rotation); /* 0..3 */
uint16_t ILI9341_Width(void);
uint16_t ILI9341_Height(void);

ILI9341_Status ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
ILI9341_Status ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
ILI9341_Status ILI9341_FillScreen(uint16_t color);
ILI9341_Status ILI9341_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
ILI9341_Status ILI9341_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
ILI9341_Status ILI9341_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
ILI9341_Status ILI9341_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/* ASCII 0x20..0x7E, 5x7 bitmap font, optional integer scale >= 1. */
ILI9341_Status ILI9341_DrawChar(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg, uint8_t scale);
ILI9341_Status ILI9341_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale);

/* Read-only font glyph accessor (5 columns, 7 pixel rows). */
uint8_t ILI9341_Font5x7Column(char ch, uint8_t column);

/* DMA TX API */
ILI9341_Status ILI9341_FillRectDMA(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint16_t color);

ILI9341_Status ILI9341_FillScreenDMA(
    uint16_t color);

/* RGB565 row-major image, w*h pixels. Pixels remain unchanged until DMA idle.
 * Clipping at right/bottom keeps the original source width as row stride.
 */
ILI9341_Status ILI9341_WriteRectDMA(uint16_t x, uint16_t y,
                                    uint16_t w, uint16_t h,
                                    const uint16_t *pixels);

void ILI9341_DMA_Service(void);

uint8_t ILI9341_DMA_IsBusy(void);
uint8_t ILI9341_DMA_HasError(void);
ILI9341_Status ILI9341_DMA_Wait(uint32_t timeout_ms);
ILI9341_Status ILI9341_DMA_Recover(void);

/* Called from HAL SPI callbacks */
void ILI9341_DMA_TxCompleteCallback(
    SPI_HandleTypeDef *hspi);

void ILI9341_DMA_ErrorCallback(
    SPI_HandleTypeDef *hspi);

#endif /* INC_ILI9341_H_ */
