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
#define ADC_CHAN_MIC 0
#define ADC_CHAN_AUX 2

#define FFT_SIZE 256
#define N_SAMPLES 1024
#define ADCCLK 48000000.0
#define Fsample 44100
//timer config
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

int32_t ADC_PERIOD = 100000; // every 100ms do 1024 ADC samples
int16_t sample_buf[N_SAMPLES];
int8_t display_bars[32]; // bars displayed on LCD

//time profiling
static uint64_t diff_adc=0;
static uint64_t diff_flush=0;
static uint64_t diff_fft=0;

//for measurig timer accuracy
int64_t times[1000];
uint16_t count = 0;
bool printanje = 1;

volatile bool timer_fired_on_100ms = false; //sets the timer

bool repeating_timer_callback(__unused struct repeating_timer *t) {
        timer_fired_on_100ms = true;
        return true;
    }
void __not_in_flash_func(adc_capture)(uint16_t *buf, size_t count) {
    adc_fifo_setup(true, false, 0, false, false);
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
    adc_select_input(ADC_CHAN_AUX);
    adc_set_clkdiv(1089); 
    
    //LCD init
   
    InitializeDisplay(BACKGROUND);
    GFX_setClearColor(ILI9341_WHITE);
    GFX_clearScreen();
    GFX_flush();   
    //timer variables 
    struct repeating_timer timer;
    add_repeating_timer_ms(-100, repeating_timer_callback, NULL, &timer); //timer which fires every 110ms
    
    //FFT variables
    static arm_rfft_instance_q15 fft_instance;
    static q15_t output[FFT_SIZE * 2];  // has to be twice FFT size
    static int16_t output_int[FFT_SIZE * 2];
    arm_status status;

   
    
    while (true) 
    {        
        if (timer_fired_on_100ms)
        { 
            uint64_t start_adc_conversion = time_us_64();
            adc_capture(sample_buf, N_SAMPLES);
            uint64_t stop_adc_conversion = time_us_64();
            diff_adc = stop_adc_conversion - start_adc_conversion;
            //printf("ADC conversion time: %u microseconds\n", time_difference);           
           
        /************************************************************************************************************
        *   Internally input is downscaled by 2 for every stage to avoid saturations inside CFFT/CIFFT process.     *
        *   Hence the output format is different for different RFFT sizes.                                          *        
        *   The input and output formats for different RFFT sizes and number of                                     *
        *   bits to upscale are mentioned in the tables below for RFFT and RIFFT:                                   *
        *    https://arm-software.github.io/CMSIS_5/DSP/html/group__RealFFT.html#ga00e615f5db21736ad5b27fb6146f3fc5 *                      
        *   RFFT Size   Input Format   Output Format Number of Bits to upscale
        *   32              
        *   64
        *   128
        *********************************************************************************************************/
        
        status = arm_rfft_init_q15(&fft_instance, 256 /*bin count*/, 0 /*forward FFT*/, 1 /*output bit order is normal*/);
        arm_rfft_q15(&fft_instance, (q15_t*)sample_buf, output);
        arm_abs_q15(output, output, FFT_SIZE);
        
        
        for(uint16_t i = 0; i < 511;i++)
        {
            output_int[i]=output[i];
        }
        for (uint8_t i = 0; i <= 31; i++) // Calculate bars on display -> 256 samples into 32 bars
        {
            uint16_t sum = 0;
            for (uint8_t j = 0; j < 8; j++) {
                sum =sum+ output_int[i * 8 + j];
            }
            sum=sum/4;
            display_bars[i] = (uint8_t)sum;  
        }
        uint64_t start_soundbar = time_us_64();
        //GFX_createFramebuf();
        for (int j = 0;j <= 31; j++)
        {             
            uint8_t percent = (uint8_t)((display_bars[j] * 100*4)/256); /*4 scaling?*/
            GFX_soundbar(j*10,240,9,240,ILI9341_BLUE,ILI9341_RED,percent);              
        }       
        GFX_flush();  
        uint64_t stop_soundbar = time_us_64();
        
        diff_flush=stop_soundbar-start_soundbar;
        printf("ADC time:   %llu ms\n soundbar time: %llu ms\nFFT time: %llu\n", diff_adc,diff_flush,diff_fft);

        
        //GFX_destroyFramebuf();
        } 
        tight_loop_contents();
    }
}
void InitializeDisplay(uint16_t color)
{
    // Initialize display
    LCD_setPins(TFT_DC, TFT_CS, TFT_RST, TFT_SCLK, TFT_MOSI);
    LCD_initDisplay();
    LCD_setRotation(TFT_ROTATION);
    GFX_createFramebuf(); //better to do it each time 
    GFX_setClearColor(color);
    GFX_setTextBack(BACKGROUND);
    GFX_setTextColor(FOREGROUND);
    GFX_clearScreen();
}

