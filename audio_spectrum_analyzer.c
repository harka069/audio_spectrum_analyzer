#include <stdio.h>
#include <string.h>
//#pragma GCC optimize ("O0") //define only if debbuging
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
#define BANDWIDTH_FACTOR 4 //2 for 20khz, 4 for 10khz
#define FFT_SIZE 1024
#define N_ADC_SAMPLES 1024
#define ADCCLK 48000000.0

//timer config
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)
#define PERIOD -70

int16_t sample_buf[N_ADC_SAMPLES];

uint16_t display_bars[256]; // bars displayed on LCD
uint16_t display_bars_max[256];
static float log_table[10000];
q15_t log_table_q15[10000];

//hann window variables
q15_t input_q15[N_ADC_SAMPLES];
q15_t hanning_window_q15[N_ADC_SAMPLES];
q15_t processed_window_q15[N_ADC_SAMPLES];
//HP filter 
static q15_t prev_input = 0;
static q15_t prev_output = 0;

//device "settings"
static uint8_t  scale_selection     = 0;
static uint8_t  scale_factor        = 255;
static uint8_t  ext_input           = 1;
static uint16_t display_bar_number  = 256;
static uint32_t bar_colour          = ILI9341_WHITE;
static uint32_t back_colour         = ILI9341_MAGENTA;
static uint32_t max_hold_colour     = ILI9341_GREEN;

//time profiling
static uint64_t diff_adc        =0;
static uint64_t diff_bars_calc  =0;
static uint64_t diff_fft        =0;
static uint64_t diff_flush      =0;
static uint64_t time_period     =0;
static uint64_t refresh         =0;

//FFT variables
arm_rfft_instance_q15 fft_instance;
q15_t output[FFT_SIZE * 2];  // has to be twice FFT size
q15_t fft_bins[FFT_SIZE/2]; 
arm_status status;

//bars
uint32_t percent;
uint32_t percent_max;
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

//ADC without HP filter
/*
void __not_in_flash_func(adc_capture)(uint16_t *buf, size_t count)
{
    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);
    for (size_t i = 0; i < count; i = i + 1)
    
    buf[i] = adc_fifo_get_blocking();
    adc_run(false);
    adc_fifo_drain();
}
*/   
//ADC with HP filter
void __not_in_flash_func(adc_capture)(int16_t *buf, size_t count) {
    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);

    for (size_t i = 0; i < count; i++) {
        // Read raw ADC value (12-bit unsigned)
        uint16_t raw_adc = adc_fifo_get_blocking();  
        q15_t input_q15 = (q15_t)(raw_adc - 2048) << 4; // Centering at 0
        
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
        float32_t f = sin((2 * PI * 4000*i)/ 44100);  
        arm_float_to_q15(&f, &input_q15[i], 1);
    }
    //build table for log scale
    for (uint16_t i= 1; i < 10000;i++)
    {
        log_table[i]=20*log10f(i)/100;
    }

    arm_float_to_q15(log_table,log_table_q15,10000);
    //Hann window generation - only preformed once
    hanning_window_init_q15(hanning_window_q15, N_ADC_SAMPLES);
    //timer 
    struct repeating_timer timer;
    // add_repeating_timer_ms(PERIOD, repeating_timer_callback, NULL, &timer); 

    
    while (true) 
    {        
        if(main_timer_fired=1)
        {   
            uint64_t start_adc_conversion = time_us_64();
            adc_capture(sample_buf, N_ADC_SAMPLES);
            uint64_t stop_adc_conversion = time_us_64();
            diff_adc = stop_adc_conversion - start_adc_conversion;
            
            switch(ext_input)
            {
                 /*SCALE factor :
                255 if ADC and song play
                10  if ADC and real sine
                7   in test sine (no ADC)*/
                case 0:
                    scale_factor = 7;
                    /*test sine generation*/
                    for (int i = 0; i < N_ADC_SAMPLES; i++)
                    {
                        float32_t f = sin((2 * PI * 4000*i)/ 44100);  
                        arm_float_to_q15(&f, &input_q15[i], 1);
                    }
                    arm_mult_q15(hanning_window_q15,input_q15,processed_window_q15, N_ADC_SAMPLES); 
                break;

                case 1:
                    scale_factor = 255;
                    adc_select_input(ADC_CHAN_MIC);
                    arm_mult_q15(hanning_window_q15,sample_buf,processed_window_q15, N_ADC_SAMPLES);
                break;

                case 2:
                    scale_factor = 255;
                    adc_select_input(ADC_CHAN_AUX);
                    arm_mult_q15(hanning_window_q15,sample_buf,processed_window_q15, N_ADC_SAMPLES);
                break;

               /* default:
                    adc_select_input(ADC_CHAN_MIC);
                    arm_mult_q15(hanning_window_q15,sample_buf,processed_window_q15, N_ADC_SAMPLES);
                break;*/
            }
            
            
            uint64_t start_fft = time_us_64();
            status = arm_rfft_init_q15(&fft_instance, FFT_SIZE, 0, 1);
            arm_rfft_q15(&fft_instance, processed_window_q15, output); //hann window
            //arm_rfft_q15(&fft_instance, sample_buf, output); //without hann window, adc
            //arm_rfft_q15(&fft_instance, input_q15, output); //without hann window, test sine
            arm_cmplx_mag_q15(output, fft_bins, FFT_SIZE/2); //from Q1.15 to Q2.14
            uint64_t stop_fft = time_us_64();
            diff_fft=stop_fft-start_fft;
           /* // Calculate bars on display -> 256 samples bars, if fewer bars, do averaging
           for (uint16_t i = 0; i <= display_bar_number-1; i++) 
            {
                uint16_t sum = 0;
                for (uint8_t j = 0; j < ((FFT_SIZE/2)/display_bar_number); j++) {
                    sum = sum + (uint16_t)output[i*((FFT_SIZE/2)/display_bar_number)+j];
                }
                display_bars[i] = ((sum/((FFT_SIZE/2)/display_bar_number))*100)/0xff;  //*2 zaradi oknjenja, *4 zaradi same knjižnice od arm = *8
                //display_bars[i] = ((sum/((FFT_SIZE/2)/display_bar_number))*100*8)/0x7fff;  //*2 zaradi oknjenja, *4 zaradi same knjižnice od arm = *8 
            }
            */
           
            for (uint16_t i = 0; i <= display_bar_number-1; i++) // Calculate bars on display -> 256 samples bars, if fewer bars, take max bar
            {
                uint16_t max_bar = 0;            
                if(scale_selection == 1)
                {   
                    for (uint16_t j = 0; j < ((FFT_SIZE/BANDWIDTH_FACTOR)/display_bar_number); j++) 
                    {
                        if (max_bar < fft_bins[i*(FFT_SIZE/BANDWIDTH_FACTOR)/display_bar_number+j])
                        {
                            max_bar = log_table_q15[fft_bins[i*(FFT_SIZE/BANDWIDTH_FACTOR)/display_bar_number+j]]; //log
                        }                   
                    }
                    display_bars[i] = max_bar;      
                }else{
                    for (uint16_t j = 0; j < ((FFT_SIZE/BANDWIDTH_FACTOR)/display_bar_number); j++) 
                    {
                        if (max_bar < fft_bins[i*(FFT_SIZE/BANDWIDTH_FACTOR)/display_bar_number+j])
                        {
                            max_bar = fft_bins[i*(FFT_SIZE/BANDWIDTH_FACTOR)/display_bar_number+j]; //lin                            
                        }                   
                    }
                    display_bars[i] = max_bar * scale_factor;//lin scale
                    
                }
                /*SCALE factor :
                255 if ADC and song play
                10  if ADC and real sine
                7   in test sine (no ADC)*/
            }

            
            uint64_t start_bars_calc = time_us_64();
            for (uint16_t i = 0;i <= display_bar_number-1; i++)
            {                
                percent = (display_bars[i] * 100) / 0x7fff;       
                GFX_soundbar(i * (256 / display_bar_number), 220, (256 / display_bar_number), 220, bar_colour, back_colour, percent);     

                
                if(display_bars_max[i] <= display_bars[i])
                {
                    display_bars_max[i] = display_bars[i];   
                }else{
                    if (display_bars_max[i] > 255) {
                        display_bars_max[i] -= 255;
                    } else {
                        display_bars_max[i] = 0;
                    }
                }
            
                /*else if(decay==2)
                {
                    decay = 0;
                    if(display_bars_max[i] > display_bars[i])
                    {
                        if (display_bars_max[i] > 0)
                        {
                            display_bars_max[i] = display_bars_max[i] - 255;                                              
                            //display_bars_max[i] = (display_bars_max[i] > 255) ? display_bars_max[i] - 255 : 0;
                        }
                    }

                }*/
                
                percent_max = (display_bars_max[i] * 100) / 0x7fff;   
                
                if (percent_max > 100)
                {
                    percent_max = 100;
                }
                GFX_drawFastHLine(i * (256 / display_bar_number), 218 - ((220 * percent_max) / 100), (256 / display_bar_number), max_hold_colour);
                  
            }
            uint64_t stop_bars_calc = time_us_64();
            diff_bars_calc = stop_bars_calc - start_bars_calc;

            if(lcd_dma_finished == 1)     
            {
                uint64_t start_flush = time_us_64();
                decay++;
                uint64_t old_refresh = refresh;
                refresh = time_us_64();
                time_period = refresh - old_refresh;
                lcd_dma_finished = 0;
                //DisplayChartLines(ILI9341_BLACK);
                GFX_setCursor(260,5);
                GFX_printf("1 stolp =");
                GFX_setCursor(260,15);
                GFX_printf("%d Hz", 22039/display_bar_number);
                GFX_flush();  
                uint64_t stop_flush = time_us_64();
                diff_flush = stop_flush - start_flush; 
            }
            //printf("\033[2J"); // Clear entire screen
            //printf("\033[H");  // Move cursor to top-left
            //printf("ADC time:   %llu us\nfft_time %llu\nflush time: %llu us\nbar calc time: %llu us\nperiod %llu us\n\n", diff_adc,diff_fft,diff_flush,diff_bars_calc,time_period);
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
                    printf("v0.9, 13.2.2025 \nKrištof Frelih\n");
                break;

                case 'b':
                    printf("Izberi usrezno stevilo stolpcev\n");
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
                        memset(display_bars, 0, sizeof(display_bars));
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

                case 'i':
                    printf("Izberi input:\n");
                    printf("0\t->\ttestni sinus\n"
                           "1\t->\tmikrofon\n"
                           "2\t->\tAUX vhod\n");                                      
                    c = getchar();
                    printf("%c\n", c);      
                    if(c < '3' && c >'/')   
                    {
                        ext_input = c - '0';
                    }
                    else{
                        printf("vtipkana stevilka je nelegalna! Poskusi znova\n");
                    }

                break;

                case 's':
                    printf("Izberi tip Y skale:\n");
                    printf("0\t->\tlinearna Y skala\n"
                           "1\t->\tlogaritemskainearna Y skala\n");                   
                    c = getchar();
                    printf("%c\n", c);      
                    if(c < '2' && c >'/')   
                    {
                        scale_selection = c - '0';
                    }else{
                    printf("vtipkana stevilka je nelegalna! Poskusi znova\n");
                    }

                break;
                case 'h':
                    printf("\033[2J"); // Clear entire screen
                    printf("\033[H");  // Move cursor to top-left
                    printf("a-bout\tinfo o izdelku\n");
                    printf("b-ars\tnastavitev zeljenega stevila stolpcev\n");
                    printf("c-olours\tnastavitev barvnega okolja\n");
                    printf("h-elp\tprikaze to sporočilo\n");
                    printf("i-nput\tizbira vhoda\n");
                    printf("s-cale\tizbira med lin in log Y skalo\n");
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

