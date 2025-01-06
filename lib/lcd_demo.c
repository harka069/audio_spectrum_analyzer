#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/rand.h"  
#include "ili9341.h"
#include "gfx.h"
#include "DisplayTest.h"
//#include "C:/Users/K_frelih/Documents/akustika/spi_lcd_demo/CMSISDSP/CMSIS-DSP/Include/arm_math.h"

//display driver by tvlad1234 https://github.com/tvlad1234/pico-displayDrivs
//GFX library by rprouse https://github.com/rprouse/ILI9341_PICO_DisplayExample
int main()
{
    stdio_init_all();
    InitializeDisplay(BACKGROUND);
    GFX_setClearColor(ILI9341_WHITE);
    GFX_clearScreen();
    while(1)
    {
    
            //GFX_clearScreen();
            for (int j = 0;j < 33; j++) //horizontala
            {   
                uint32_t random = get_rand_32();
                uint8_t percent = (uint8_t)((random / (float)0xFFFFFFFF) * 100);
                GFX_soundbar(j*10,240,9,240,ILI9341_BLUE,ILI9341_RED,percent);               
            }
            
            GFX_flush();   
            //sleep_ms(1);
    }
    return 0;
}

void InitializeDisplay(uint16_t color)
{
    // Initialize display
    LCD_setPins(TFT_DC, TFT_CS, TFT_RST, TFT_SCLK, TFT_MOSI);
    LCD_initDisplay();
    LCD_setRotation(TFT_ROTATION);
    GFX_createFramebuf();
    GFX_setClearColor(color);
    GFX_setTextBack(BACKGROUND);
    GFX_setTextColor(FOREGROUND);
    GFX_clearScreen();
}

