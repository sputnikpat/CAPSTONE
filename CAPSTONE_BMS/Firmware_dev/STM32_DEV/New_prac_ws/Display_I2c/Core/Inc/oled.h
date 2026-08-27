/*
 * oled.h
 *
 *  Created on: Aug 23, 2026
 *      Author: harsh
 */

#ifndef OLED_H
#define OLED_H

#include "main.h"
#include <stdint.h>

#define OLED_ADDRESS 0x3C
#define OLED_WIDTH   128
#define OLED_HEIGHT  64

void OLED_Init(void);

void OLED_I2C_Recover(void);

void OLED_WriteCommand(uint8_t command);
void OLED_WriteData(uint8_t data);

void OLED_Clear(void);
void OLED_Fill(uint8_t pattern);
void OLED_Update(void);

void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t color);
void OLED_DrawChar(uint8_t x, uint8_t y, char c);
void OLED_DrawString(uint8_t x, uint8_t y, const char *str);

void OLED_BootScreen(void);

#endif
