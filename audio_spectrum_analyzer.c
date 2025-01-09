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
//timer config
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

int32_t ADC_PERIOD = 100000; // every 100ms do 1024 ADC samples
int16_t sample_buf[N_SAMPLES];
int8_t display_bars[32]; // bars displayed on LCD
//for measurig timer accuracy
int64_t times[1000];
uint16_t count = 0;
bool printanje = 1;
// Alarm interrupt handler
static volatile bool alarm_fired;

static void alarm_irq(void) {
    
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM); // Clear the alarm irq 
    /*
    uint64_t absolutetime= time_us_64(); 
    printf("alarm fired at abs time : %llu\n", absolutetime);
    */
    if (count<1000)
    {
        times[count]=time_us_64(); 
        count++;
        alarm_fired = true;
        
    }else
    {
        alarm_fired = false;
    }

    /* arm the alarm alarm_fired = false;*/
     
}
static void alarm_in_us(uint32_t delay_us) {                                                  
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM); // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs) 
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq); // Set irq handler for alarm irq
    irq_set_enabled(ALARM_IRQ, true); // Enable the alarm irq
    uint64_t target = timer_hw->timerawl + delay_us; // Enable interrupt in block and at processor Alarm is only 32 bits so if trying to delay more than that need to be careful and keep track of the upper bits
    timer_hw->alarm[ALARM_NUM] = (uint32_t) target;  // Write the lower 32 bits of the target time to the alarm which will arm it
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
    adc_select_input(ADC_CHANELL);
    adc_set_clkdiv(1089); 
    
    //LCD init
    InitializeDisplay(BACKGROUND);
    GFX_setClearColor(ILI9341_WHITE);
    GFX_clearScreen();
    GFX_flush();   
    //timer variables 
    alarm_fired = false; //sets the timer
    alarm_in_us(ADC_PERIOD); 
    //FFT variables
    static arm_rfft_instance_q15 fft_instance;
    static q15_t output[FFT_SIZE * 2];  // has to be twice FFT size
    static int16_t output_int[FFT_SIZE * 2];
    arm_status status;
    

    //time profiling
    static uint64_t diff_adc=0;
    static uint64_t diff_flush=0;
    while (true) 
    {        
        if(alarm_fired == true){
        // Wait for alarm to fire
            uint64_t start_adc_conversion = time_us_64();
            adc_capture(sample_buf, N_SAMPLES);
            uint64_t stop_adc_conversion = time_us_64();
            diff_adc = stop_adc_conversion - start_adc_conversion;
            //printf("ADC conversion time: %u microseconds\n", time_difference);
            alarm_fired = false;
            alarm_in_us(ADC_PERIOD);            
        }

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
            for (uint8_t j = 0; j < 4; j++) {
                sum =sum+ output_int[i * 4 + j];
            }
            sum=sum/4;
            display_bars[i] = (uint8_t)sum;  
        }
        for (int j = 0;j <= 31; j++)
        {   
            uint8_t percent = (uint8_t)((display_bars[j] * 100*4)/256); /*4 scaling?*/
            GFX_soundbar(j*10,240,9,240,ILI9341_BLUE,ILI9341_RED,percent);              
        }

        //time profiling
        uint64_t start_flush = time_us_64();
        GFX_flush();  
        uint64_t stop_flush = time_us_64();
        diff_flush=stop_flush-start_flush;
        //printf("ADC time:   %llu ms\nFLUSH time: %llu ms\n", diff_adc,diff_flush);
        
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

