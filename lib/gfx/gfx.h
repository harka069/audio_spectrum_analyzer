#ifndef gfx_H
#define gfx_H

#include "pico/stdlib.h"
#include "gfxfont.h"

// convert 8 bit r, g, b values to 16 bit colour (rgb565 format) 
#define GFX_RGB565(R, G, B) ((uint16_t)(((R) & 0b11111000) << 8) | (((G) & 0b11111100) << 3) | ((B) >> 3))

void GFX_createFramebuf();
void GFX_destroyFramebuf();

/*draws a single pixel*/
void GFX_drawPixel(int16_t x, int16_t y, uint16_t color);

/*puts a single character on screen*/
void GFX_drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size_x, uint8_t size_y);

/*writes a character to the screen, handling the cursor position and text wrapping automatically*/
void GFX_write(uint8_t c);

/* places the text cursor at the specified coordinates*/
void GFX_setCursor(int16_t x, int16_t y);

/*sets the text color*/
void GFX_setTextColor(uint16_t color);

/* sets the text background color*/
void GFX_setTextBack(uint16_t color);

/*sets the used font, using the same format as Adafruit-GFX-Library*/
void GFX_setFont(const GFXfont *f);

/*draws a line from (x0,y0) to (x1,y1)*/
void GFX_drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/*draws a vertical line*/
void GFX_drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);

/* draws a horizontal line*/
void GFX_drawFastHLine(int16_t x, int16_t y, int16_t l, uint16_t color);

/*draws a rectangle*/
void GFX_drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/*draws a filled rectangle*/
void GFX_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/* fills the screen with a specified color*/
void GFX_fillScreen(uint16_t color);

/*sets the color the screen should be cleared with*/
void GFX_setClearColor(uint16_t color);

/* clears the screen, filling it with the color specified using the function above*/
void GFX_clearScreen();

/* draws a circle*/
void GFX_drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/*draws a filled circle*/
void GFX_fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/*prints formatted text*/
void GFX_printf(const char *format, ...);
void GFX_flush();
void GFX_Update();
void GFX_scrollUp(int n);

uint GFX_getWidth();
uint GFX_getHeight();

//made by KF
/*create a filled square with 2 colours. Ratio is defined by percent, color1 is at bottom of screen*/
void GFX_soundbar(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color1,uint16_t color2,uint8_t percent);



#endif
