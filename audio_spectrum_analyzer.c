#include <stdio.h>

//high level libs
#include "pico/stdlib.h"
#include "pico/rand.h"  
//low level libs
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

#include "hardware/uart.h"

//display libs
#include "ili9341.h"
#include "gfx.h"
//display config
#include "DisplayTest.h"
//fft library
#include "arm_math.h"
//ADC config
#define ADC_CHAN 0
#define ADC_CHANELL 2

#define FFT_SIZE 256
#define N_SAMPLES 1024
#define ADCCLK 48000000.0
#define Fsample 44100

int16_t sample_buf[N_SAMPLES];

int8_t display_bars[31];

void __not_in_flash_func(adc_capture)(uint16_t *buf, size_t count) {
    adc_fifo_setup(true, false, 0, false, false);
    adc_set_clkdiv(1089); //realvalue is 1089
    adc_run(true);
    for (size_t i = 0; i < count; i = i + 1)
        buf[i] = adc_fifo_get_blocking();
    adc_run(false);
    adc_fifo_drain();
}


int main()
{
    stdio_init_all();


    adc_gpio_init(28);
    adc_init();
    adc_select_input(ADC_CHANELL);
    //adc_set_clkdiv(100000); //realvalue is 1089

    //LCD init
    InitializeDisplay(BACKGROUND);
    GFX_setClearColor(ILI9341_WHITE);
    GFX_clearScreen();
    GFX_flush();   
    
    static arm_rfft_instance_q15 fft_instance;
    static q15_t output[FFT_SIZE * 2];  // has to be twice FFT size
    static int16_t output_int[FFT_SIZE * 2];

    arm_status status;
    
    

  
    while (true) 
    {

        /*random values on display*/
        /*for (int j = 0;j < 33; j++)            {   
                uint32_t random = get_rand_32();
                uint8_t percent = (uint8_t)((random / (float)0xFFFFFFFF) * 100);
                GFX_soundbar(j*10,240,9,240,ILI9341_BLUE,ILI9341_RED,percent);               
            }
        GFX_flush();  */ 
        /*char c = getchar();
        printf("%c", c);

        switch (c) {
            case 's': {
                uint32_t result = adc_read();
                const float conversion_factor = 3.3f / (1 << 12);
                printf("\n0x%03x -> %f V\n", result, result * conversion_factor);
                break;
            }
            case 'S': {*/
                printf("\nStarting capture\n");
                uint64_t start_adc_conversion = time_us_64();
                adc_capture(sample_buf, N_SAMPLES);
                uint64_t stop_adc_conversion = time_us_64();
                uint32_t time_difference = stop_adc_conversion - start_adc_conversion;
                printf("ADC conversion time: %u microseconds\n", time_difference);
                status = arm_rfft_init_q15(&fft_instance, 256 /*bin count*/, 0 /*forward FFT*/, 1 /*output bit order is normal*/);
                arm_rfft_q15(&fft_instance, (q15_t*)sample_buf, output);
                arm_abs_q15(output, output, FFT_SIZE);
                for(uint16_t i = 0; i < 511;i++)
                {
                    output_int[i]=output[i];
                }
                for (uint8_t i = 0; i <= 31; i++) 
                {
                    uint16_t sum = 0;
                    for (uint8_t j = 0; j < 4; j++) {
                        sum =sum+ output_int[i * 4 + j];
                    }
                    sum=sum/4;
                    display_bars[i] = (uint8_t)sum;  // Calculate average
                }
                for (int j = 0;j <= 31; j++)
                {   
                uint8_t percent = (uint8_t)((display_bars[j] * 100*4/*4 samo skalira*/)/256);
                GFX_soundbar(j*10,240,9,240,ILI9341_BLUE,ILI9341_RED,percent);              
                }
                GFX_flush();  
                /*break;
            }
            
            case '\n':
            case '\r':
                break;
            default:
                printf("\nUnrecognised command: %c\n", c);
                break;
        }*/
             
    }
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