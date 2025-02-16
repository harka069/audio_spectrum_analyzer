#include <stdio.h>

#define ARM_MATH_CM0PLUS

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

#include "tusb.h"
//display libs
#include "ili9341.h"
#include "gfx.h"
//display config
#include "DisplayTest.h"
//fft library
#include "arm_math.h"
#include <math.h>
//ADC config
#define ADC_CHAN_MIC 0
#define ADC_CHAN_AUX 2

#define FFT_SIZE 256
#define N_ADC_SAMPLES 1024
#define ADCCLK 48000000.0

//timer config
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)
#define PERIOD -45

int32_t ADC_PERIOD = 100000; // every 100ms do 1024 ADC samples
int16_t sample_buf[N_ADC_SAMPLES];

int8_t display_bars[256]; // bars displayed on LCD

// global variables
q15_t input_q15[N_ADC_SAMPLES];
q15_t hanning_window_q15[N_ADC_SAMPLES];
q15_t processed_window_q15[N_ADC_SAMPLES];
float32_t hanning_window_test[N_ADC_SAMPLES];

//device "settings"
static uint16_t display_bar_number  = 32;
static uint32_t bar_colour          = ILI9341_BLUE;
static uint32_t back_colour         = ILI9341_RED;


//time profiling
static uint64_t diff_adc        =0;
static uint64_t diff_bars_calc  =0;
static uint64_t diff_flush      =0;
static uint64_t time_period     =0;
static uint64_t refresh         =0;

 //FFT variables
arm_rfft_instance_q15 fft_instance;
q15_t output[FFT_SIZE * 2];  // has to be twice FFT size
arm_status status;

volatile bool main_timer_fired = false; //sets the timer
volatile bool lcd_dma_finished = true;

// Function prototypes
void hanning_window_init_q15(q15_t* hanning_window_q15, size_t size);
void color_coding(char c, uint32_t *setting);
void InitializeDisplay(uint16_t color);

bool repeating_timer_callback(__unused struct repeating_timer *t) 
{
    main_timer_fired = true;
    return true;
    //uint64_t start_adc_conversion1 = time_us_64();
    //printf("start of timer: %llu us\n", start_adc_conversion1);    
}
void __not_in_flash_func(adc_capture)(uint16_t *buf, size_t count)
{
    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);
    for (size_t i = 0; i < count; i = i + 1)
        buf[i] = adc_fifo_get_blocking();
    adc_run(false);
    adc_fifo_drain();
}
void dma_handler()
{
    lcd_dma_finished = true; 
    ILI9341_DeSelect();
    dma_hw->ints0 = 1u << dma_tx;
    //dma_channel_start(dma_tx);
}
int main()
{   
    stdio_init_all();
    adc_gpio_init(28);
    adc_init();
    adc_select_input(ADC_CHAN_MIC);
    adc_set_clkdiv(1089); 
    
    //LCD init
    InitializeDisplay(BACKGROUND);  

   

    //test purpose sine generation
    for (int i = 0; i < N_ADC_SAMPLES; i++) {
        float32_t f = sin((2 * PI * 4000) / 44100 * i);  
        arm_float_to_q15(&f, &input_q15[i], 1);
    }

    //Hann window generation - only preformed once
    hanning_window_init_q15(hanning_window_q15, N_ADC_SAMPLES);
    arm_mult_q15(hanning_window_q15,input_q15, processed_window_q15, N_ADC_SAMPLES);
    
    //timer 
    struct repeating_timer timer;
    add_repeating_timer_ms(PERIOD, repeating_timer_callback, NULL, &timer); 

    
    while (true) 
    {        
        if(main_timer_fired)
        {   
            
            uint64_t start_adc_conversion = time_us_64();
            adc_capture(sample_buf, N_ADC_SAMPLES);
            uint64_t stop_adc_conversion = time_us_64();
            diff_adc = stop_adc_conversion - start_adc_conversion;
            //start fft
           

            status = arm_rfft_init_q15(&fft_instance, 256 /*bin count*/, 0 /*forward FFT*/, 1 /*output bit order is normal*/);
            arm_rfft_q15(&fft_instance, processed_window_q15, output);
            //arm_rfft_q15(&fft_instance, (q15_t*)sample_buf, output);
            arm_cmplx_mag_q15(output, output, FFT_SIZE/2);
            //arm_abs_q15(output, output, FFT_SIZE);
            output[0]=0;
            output[1]=0; //remove dc component

            for (uint16_t i = 0; i <= display_bar_number-1; i++) // Calculate bars on display -> 256 samples bars
            {
                uint16_t sum = 0;
                for (uint8_t j = 0; j < (256/display_bar_number); j++) {
                    sum =sum+ output[i * (256/display_bar_number) + j];
                }
                sum=sum/(256/display_bar_number);
                display_bars[i] = (uint8_t)sum;  
            }

            uint64_t start_bars_calc = time_us_64();
            for (int j = 0;j < display_bar_number; j++)
            {             
                uint8_t percent = (uint8_t)((display_bars[j])); /*4 scaling?  * 100*4)/256*/
                GFX_soundbar(j*(256/display_bar_number),220,(256/display_bar_number),220,bar_colour,back_colour,percent);              
            }

            uint64_t stop_bars_calc = time_us_64();
            diff_bars_calc = stop_bars_calc - start_bars_calc;
            if(lcd_dma_finished == 1)     
            {
                uint64_t old_refresh = refresh;
                refresh = time_us_64();
                time_period = refresh -old_refresh;
                lcd_dma_finished = 0;
                GFX_setCursor(0,230);
                GFX_printf("1 stolpec = %d Hz", 21050/display_bar_number);
                GFX_flush();  
                
            }
            //printf("ADC time:   %llu us\nflush time: %llu us\nbar calc time: %llu us\nperiod %llu us\n\n", diff_adc,diff_flush,diff_bars_calc,time_period);
            main_timer_fired = false;
        }
        if (tud_cdc_available()) 
        {
            char c = getchar();
            char d = '0';
            printf("%c\n", c);
            switch (c) 
            {   
                case 'a':
                    printf("Akusticni spektralni analizator, narejen pri predmetu Akustika na Fakulteti za elektrotehniko.\n");
                    printf("v0.9, \13.2.2025 \nKrištof Frelih\n");
                break;
                case 'b':
                    printf("Izberi usrezbi stevilo stolpcev\n");
                    printf( "2\t->\t2\n"
                            "4\t->\t3\n"
                            "8\t->\t4\n"
                            "16\t->\t5\n"
                            "32\t->\t6\n"
                            "64\t->\t7\n"
                            "128\t->\t8\n"
                            "256\t->\t9\n");
                    c = getchar();
                    printf("%c\n", c);      
                    if(c < ':' && c >'1')             
                    {
                        display_bar_number = 1 << (c-'1');
                        printf("Frekvencni spekter bo prikazovalo: %d stolpcev\n", display_bar_number);
                        GFX_clearScreen();
                    }
                    else
                    {
                        display_bar_number = 32;
                        printf("vtipkana stevilka je nelegalna! Poskusi znova\n");
                    }
                break;  
                case 'c':
                    printf("Nastavi barvno okolje:\n");
                    printf("barvna koda:\n"
                            "BLACK\t->\t0\n"
                            "WHITE\t->\t1\n"
                            "RED\t->\t2\n"
                            "GREEN\t->\t3\n"
                            "BLUE\t->\t4\n"
                            "CYAN\t->\t5\n"
                            "MAGENTA\t->\t6\n"
                            "YELLOW\t->\t7\n"
                            "ORANGE\t->\t8\n");
                    printf("Vpisi barvno kodo stolpcev:\n");
                    c = getchar();
                    printf("%c\n", c);      
                    if(c < ':' && c >'/')   
                    {
                        color_coding(c,&bar_colour);
                    }
                    else
                    {
                        bar_colour= ILI9341_BLUE;
                        printf("vtipkana stevilka je nelegalna! Poskusi znova\n");
                    }  
                    printf("Vpisi barvno odzadja:\n");
                    c = getchar();
                    printf("%c\n", c);      
                    if(c < ':' && c >'/')   
                    {
                        color_coding(c,&back_colour);
                    }
                    else
                    {
                        back_colour= ILI9341_RED;
                        printf("vtipkana stevilka je nelegalna! Poskusi znova\n");
                    }  
                    GFX_setClearColor(back_colour); 
                    GFX_setTextBack(back_colour);
                    GFX_setTextColor(bar_colour);
                    GFX_clearScreen();
                break;
                case 'h':
                    printf("a - info o izdelku\n");
                    printf("b - nastavitev zeljenega stevila stolpcev \n");
                    printf("c - nastavitev barvnega okolja \n");
                    
                break;   

                default:
                    printf("znak ni razpoznan - poskusi znova");
                break;
            }
        }
    }
}
void InitializeDisplay(uint16_t color)
{
    // Initialize display
    LCD_setPins(TFT_DC, TFT_CS, TFT_RST, TFT_SCLK, TFT_MOSI);
    LCD_initDisplay();
    LCD_setRotation(TFT_ROTATION);
    GFX_createFramebuf(); //better to do it each time 
    
    dma_channel_set_irq0_enabled(dma_tx, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    //dma_handler();

    GFX_setClearColor(ILI9341_WHITE);
    GFX_clearScreen();
    GFX_setTextColor(ILI9341_BLACK);
    GFX_setTextBack(ILI9341_WHITE);
    //GFX_setFont(&glyph);
    GFX_setCursor(0,230);
    GFX_printf("20Hz");
    GFX_setCursor(290,230);
    GFX_printf("20kHz");
    GFX_flush(); 
}

void color_coding(char c,uint32_t *setting)
{
    switch(c)
    {
        case('0'):
            *setting = ILI9341_BLACK;
        break;
        case('1'):
            *setting = ILI9341_WHITE;
        break;
        case('2'):
            *setting = ILI9341_RED;
        break;
        case('3'):
            *setting = ILI9341_GREEN;
        break;                            
        case('4'):
            *setting = ILI9341_BLUE;
        break;
        case('5'):
            *setting = ILI9341_CYAN;
        break;
        case('6'):
            *setting = ILI9341_MAGENTA;
        break;
        case('7'):
            *setting = ILI9341_YELLOW;
        break;
        case('8'):
            *setting = ILI9341_ORANGE;
        break;
    }
}

void hanning_window_init_q15(q15_t* hanning_window_q15, size_t size) {
    for (size_t i = 0; i < size; i++) {
      // calculate the Hanning Window value for i as a float32_t
      float32_t f = 0.5 * (1.0 - arm_cos_f32(2 * PI * i / size ));
  
      // convert value for index i from float32_t to q15_t and store
      // in window at position i
      hanning_window_test[i]=f;
      arm_float_to_q15(&f, &hanning_window_q15[i], 1);
    }
  }