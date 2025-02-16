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
//IIR HP filter
#define ALPHA_Q15 0x7F5C  // 0.995 in Q1.15 format

#define FFT_SIZE 512
#define N_ADC_SAMPLES 1024
#define ADCCLK 48000000.0

//timer config
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)
#define PERIOD -45

int32_t ADC_PERIOD = 100000; // every 100ms do 1024 ADC samples
int16_t sample_buf[N_ADC_SAMPLES];

uint16_t display_bars[256]; // bars displayed on LCD
uint16_t display_bars_max[256];
const float conversion_factor = 3.3f / (1 << 12);
//hann window variables
q15_t input_q15[N_ADC_SAMPLES];
q15_t hanning_window_q15[N_ADC_SAMPLES];
q15_t processed_window_q15[N_ADC_SAMPLES];

//device "settings"
static uint16_t display_bar_number  = 256;
static uint32_t bar_colour          = ILI9341_WHITE;
static uint32_t back_colour         = ILI9341_MAGENTA;
static uint32_t max_hold_colour     = ILI9341_GREEN;

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

//bars
uint8_t percent;
uint8_t percent_max;
uint8_t decay;
volatile bool main_timer_fired = false; //sets the timer
volatile bool lcd_dma_finished = true;

//Function prototypes
void hanning_window_init_q15(q15_t* hanning_window_q15, size_t size);
void color_coding(char c, uint32_t *setting);
void InitializeDisplay(uint16_t color);
void DisplayChartLines(uint16_t color);

bool repeating_timer_callback(__unused struct repeating_timer *t) 
{
    main_timer_fired = true;
    return true;
    //uint64_t start_adc_conversion1 = time_us_64();
    //printf("start of timer: %llu us\n", start_adc_conversion1);    
}
/*
void __not_in_flash_func(adc_capture)(uint16_t *buf, size_t count)
{
    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);
    for (size_t i = 0; i < count; i = i + 1)
    
    buf[i] = adc_fifo_get_blocking();
    adc_run(false);
    adc_fifo_drain();
}*/


static q15_t prev_input = 0;
static q15_t prev_output = 0;

void __not_in_flash_func(adc_capture)(uint16_t *buf, size_t count) {
    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);

    for (size_t i = 0; i < count; i++) {
        // Read raw ADC value (12-bit unsigned)
        uint16_t raw_adc = adc_fifo_get_blocking();  
        q15_t input_q15 = (q15_t)(raw_adc - 2048) << 3; // Centering at 0

        //1st order HP FIR (DC removal): y[n] = x[n] - x[n-1] + alpha * y[n-1]
        int32_t temp = (int32_t)input_q15 - (int32_t)prev_input + 
                       ((int32_t)ALPHA_Q15 * (int32_t)prev_output >> 15);

        buf[i] = __SSAT(temp, 16);  // Saturation to Q15

        // Update previous values
        prev_input = input_q15;
        prev_output = buf[i];
    }
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
            arm_mult_q15(hanning_window_q15,sample_buf,processed_window_q15, N_ADC_SAMPLES);
            arm_shift_q15(processed_window_q15,1,processed_window_q15,N_ADC_SAMPLES); //magintude correction (*2) becouse of hann window
            
            status = arm_rfft_init_q15(&fft_instance, 512 /*bin count*/, 0 /*forward FFT*/, 1 /*output bit order is normal*/);
            arm_rfft_q15(&fft_instance, processed_window_q15, output); //hann window
            //arm_rfft_q15(&fft_instance, sample_buf, output); //without hanno window
            arm_cmplx_mag_q15(output, output, FFT_SIZE/2);
            
            for (uint16_t i = 0; i <= display_bar_number-1; i++) // Calculate bars on display -> 256 samples bars
            {
                uint16_t sum = 0;
                for (uint8_t j = 0; j < ((FFT_SIZE/2)/display_bar_number); j++) {
                    sum = sum + (uint16_t)output[i*((FFT_SIZE/2)/display_bar_number)+j];
                }
                display_bars[i] = sum/(FFT_SIZE/display_bar_number);  
            }

            uint64_t start_bars_calc = time_us_64();

            for (int j = 0;j < display_bar_number; j++)
            {                
                percent = (display_bars[j]*100)/256;                
                /*float log_x = log10((float)(j + 1));  // Avoid log(0) by adding 1
                int log_scaled_x = (int)((log_x / log10(display_bar_number + 1)) * 256);  // Scale to screen width
                int dynamic_width = (int)((log_scaled_x / 256.0) * 20);
                GFX_soundbar(log_scaled_x,220,dynamic_width,220,bar_colour,back_colour,percent); */    
                GFX_soundbar(j*(256/display_bar_number),220,(256/display_bar_number),220,bar_colour,back_colour,percent);     
                if(display_bars_max[j] < display_bars[j])
                {
                    display_bars_max[j]=display_bars[j];
                           
                }
                percent_max = (display_bars_max[j]*100)/256;
                //GFX_drawFastHLine(log_scaled_x,219-(220*percent_max)/100,dynamic_width,max_hold_colour);
                GFX_drawFastHLine(j*(256/display_bar_number),219-(220*percent_max)/100,(256/display_bar_number),max_hold_colour);
                if (decay<=2)
                {
                    decay = 0;
                        if (display_bars_max[j]>0)
                        display_bars_max[j]--;                                              
                }
            }
           
           
            uint64_t stop_bars_calc = time_us_64();
            diff_bars_calc = stop_bars_calc - start_bars_calc;
            if(lcd_dma_finished == 1)     
            {
                decay++;
                uint64_t old_refresh = refresh;
                refresh = time_us_64();
                time_period = refresh -old_refresh;
                lcd_dma_finished = 0;
                //DisplayChartLines(ILI9341_BLACK);
                GFX_setCursor(260,5);
                GFX_printf("1 stolp =");
                GFX_setCursor(260,15);
                GFX_printf("%d Hz", 22039/display_bar_number);
                GFX_flush();  
                
            }

            printf("ADC time:   %llu us\nflush time: %llu us\nbar calc time: %llu us\nperiod %llu us\n\n", diff_adc,diff_flush,diff_bars_calc,time_period);
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
                        memset(display_bars_max, 0, sizeof(display_bars_max));
                        GFX_clearScreen();
                        DisplayChartLines(ILI9341_BLACK);
                        GFX_flush(); 
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
                        bar_colour= ILI9341_WHITE;
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
                        back_colour= ILI9341_MAGENTA;
                        printf("vtipkana stevilka je nelegalna! Poskusi znova\n");
                    }  
                    printf("Vpisi barvno max hold indikatorjev:\n");
                    c = getchar();
                    printf("%c\n", c);      
                    if(c < ':' && c >'/')   
                    {
                        color_coding(c,&max_hold_colour);
                    }
                    else
                    {
                        back_colour= ILI9341_RED;
                        printf("vtipkana stevilka je nelegalna! Poskusi znova\n");
                    }  
                    
                    GFX_setClearColor(back_colour); 
                    GFX_setTextBack(back_colour);
                    //GFX_setTextColor(bar_colour);
                    GFX_clearScreen();
                    DisplayChartLines(ILI9341_BLACK);
                    GFX_flush(); 
            
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

    GFX_setClearColor(back_colour); 
    GFX_setTextBack(back_colour);
    GFX_setTextColor(ILI9341_BLACK);
    GFX_clearScreen();
    DisplayChartLines(ILI9341_BLACK);

    GFX_flush(); 
}
void DisplayChartLines(uint16_t color) {
    GFX_drawFastHLine(0, 221, 256, color);
    GFX_drawFastHLine(0, 222, 256, color);
    GFX_drawFastHLine(0, 223, 256, color);
    GFX_drawFastHLine(0, 224, 256, color);
    GFX_drawFastVLine(256, 224, -224, color);
    GFX_drawFastVLine(257, 224, -224, color);
    GFX_drawFastVLine(258, 224, -224, color);
    for (int i=0; i<=256; i=i+50)
    {
        GFX_drawFastVLine(i,224, 5, color);
        GFX_drawFastVLine(i+1,224, 5, color);
        GFX_setCursor(i+1,231);
        GFX_printf("%.1f",(float32_t)(i*86)/1000);
    }
    GFX_setCursor(262,231);
    GFX_printf("kHZ");
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
      
      arm_float_to_q15(&f, &hanning_window_q15[i], 1);
    }
}

