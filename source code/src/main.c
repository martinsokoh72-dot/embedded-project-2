// ------------------------------------------------------------
// System: STM32L432 Fan Control with Encoder + Tach Feedback
//
// Functionality:
// - Rotary encoder (TIM2) sets PWM duty cycle (TIM16 on PA6)
// - Fan tach signal (PB4 interrupt) measures speed
// - Speed displayed via serial + SPI display
// - LED indicates maximum speed condition
// ------------------------------------------------------------
#include <stm32l432xx.h>
#include <stdint.h>
#include <math.h>

void setup(void);
void delay(volatile uint32_t dly);
void enablePullUp(GPIO_TypeDef *Port, uint32_t BitNumber);
void pinMode(GPIO_TypeDef *Port, uint32_t BitNumber, uint32_t Mode);
void initADC(void);
int readADC(int chan);
void initTimer2(void);
void setTimer2Duty(int duty);
void initClocks(void);
void delay_ms(unsigned dly);
void initEncoder(void);
void initBuzzerTimer(void);

// added from serial file
#include <eeng1030_lib.h>
#include <stdio.h>
#include  <errno.h>
#include  <sys/unistd.h> // STDOUT_FILENO, STDERR_FILENO

#include "display.h"


void initSerial(uint32_t baudrate);
void eputc(char c);

// define needed constants
#define pulse_per_rev 2

#define BEEP_SAMPLES 4790
int16_t audioBuffer[BEEP_SAMPLES * 2];
volatile uint8_t playing = 0;// 0 = buzzer off, 1 = buzzer on (controlled by TIM6)

int vin, speed_hz, speed_rpm;
volatile unsigned pulse_count, milliseconds;

// ------------------------------------------------------------
// TIM16 - PWM output for fan speed control on PA6
// ------------------------------------------------------------
void initTimer16()
{
    RCC->AHB2ENR |= (1 << 0);          // GPIOA clock on — must be before AFR access
    RCC->APB2ENR |= (1 << 17);         // TIM16 clock on

    // Manually configure PA6 as AF14 — do NOT use selectAlternateFunction
    GPIOA->AFR[0] &= ~(0xF << 24);     // clear PA6 AF bits (bits 27:24)
    GPIOA->AFR[0] |=  (14  << 24);     // set AF14

    GPIOA->MODER  &= ~(3u << 12);      // clear PA6 mode bits
    GPIOA->MODER  |=  (2u << 12);      // set alternate function mode

    // CHECK IMMEDIATELY AFTER WRITING
    printf("MODER inside initTimer16: %lu\r\n", (GPIOA->MODER >> 12) & 0x3);

    TIM16->CR1   = 0;
    TIM16->PSC   = 0;
    TIM16->ARR   = 3199;               // 80MHz / 3200 = 25kHz
    TIM16->CCMR1 = (0b110 << 4) | (1 << 3) | (1 << 2);
    TIM16->CCER  = (1 << 0);           // CC1E
    TIM16->CCR1  = 1600;               // 50% duty
    TIM16->BDTR  = (1 << 15);          // MOE
    TIM16->EGR  |= (1 << 0);
    TIM16->CR1   = (1 << 7) | (1 << 0);

    // CHECK AGAIN AT END OF FUNCTION
    printf("MODER end of initTimer16: %lu\r\n", (GPIOA->MODER >> 12) & 0x3);
}
// ------------------------------------------------------------
// TIM6 - Buzzer tone generator (toggles PB6 at 500Hz)
// ------------------------------------------------------------
void initBuzzerTimer(void)
{
    RCC->APB1ENR1 |= (1 << 4);   // enable TIM6 clock

    TIM6->CR1  = 0;
    TIM6->PSC  = 7999;            // 80MHz / 8000 = 10kHz
    TIM6->ARR  = 9;               // 10kHz / 10 = 1kHz toggle → 500Hz buzz tone
    TIM6->DIER = (1 << 0);        // enable update interrupt
    TIM6->EGR  = (1 << 0);        // generate update event
    TIM6->CR1  = (1 << 0);        // start timer

    NVIC->ISER[1] |= (1 << 22);   // TIM6 IRQ = 54, bit 22 of ISER[1]
}

int main()
{
    setup();
    SysTick->LOAD = 80000-1; // Systick clock = 80MHz. 80000000/80000=1000
	SysTick->CTRL = 7; // enable systick counter and its interrupts
	SysTick->VAL = 10; // start from a low number so we don't wait for ages for first interrupt

    initEncoder(); // TIM2 (input)
    init_display(); // SPI display
    initTimer16(); //TIM16 (PWM)
    initBuzzerTimer();      // TIM6 - buzzer tone generator

    delay_ms(2000);

    printf("MODER PA6: %lu\r\n",  (GPIOA->MODER >> 12) & 0x3);  // should be 2
    printf("AFR PA6:   %lu\r\n",  (GPIOA->AFR[0] >> 24) & 0xF); // should be 14
    printf("TIM16 CR1:  %lu\r\n", TIM16->CR1);   // should be 0x81
    printf("TIM16 CCER: %lu\r\n", TIM16->CCER);  // should be 0x1
    printf("TIM16 BDTR: %lu\r\n", TIM16->BDTR);  // should be 0x8000
    printf("TIM16 ARR:  %lu\r\n", TIM16->ARR);   // should be 3199
    printf("TIM16 CCR1: %lu\r\n", TIM16->CCR1);  // should be 1600
    
    while(1)
    {        
        vin = TIM2->CNT;

        TIM16->CCR1 = (vin * 3199) / 159;

        //uncomment
        uint32_t pulses;

        __disable_irq();
        pulses = pulse_count;

        pulse_count = 0;

        //uncomment
        __enable_irq();
    
        // evaluate speed 
        speed_hz = (pulses / pulse_per_rev);
        speed_rpm = (speed_hz * 450);

        // LED: ON when speed is safe (<=3500), OFF when too fast
        if (speed_rpm <= 3500)
        {
            GPIOB->ODR |= (1 << 5);  // turn LED on — normal speed
            playing = 0;             // buzzer off
        }
        else
        {
            GPIOB->ODR &= ~(1 << 5); // turn LED off — overspeed
            playing = 1;               // buzzer on via TIM6
        
        }

        // print fan speed 
        printf("ENC: %d, CCR1: %lu, RPM: %d,buzz: %lu\r\n", vin, TIM16->CCR1, speed_rpm,(GPIOB->ODR >> 6) & 1);

        // Clear text areas for display
        fillRectangle(0,10,160,20,RGBToWord(0,0,0));
        fillRectangle(0,30,160,20,RGBToWord(0,0,0));

        // Display values
        char buffer[50];

        sprintf(buffer, "pos: %3d", vin);
        printText(buffer, 5, 10, RGBToWord(255,255,0), 0);

        sprintf(buffer, "RPM: %4d", speed_rpm);
        printText(buffer, 5, 30, RGBToWord(0,255,0), 0);

        delay_ms(100);
    }
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup(void)
{
    initClocks();
    RCC->AHB2ENR |= (1 << 0) | (1 << 1); // turn on GPIOA and GPIOB

    pinMode(GPIOB,3,1); // PB3 digital output
    pinMode(GPIOB,4,0); // PB4 digital input (tachometer)
    
    pinMode(GPIOB, 5, 1); // PB5 output for LED

    pinMode(GPIOB, 6, 1); // PB6 as output for buzzer

    enablePullUp(GPIOB,4); // pull-up for tach input

    //to comment the below to initserial
    //pinMode(GPIOA,0,3);  // analog input
    
    pinMode(GPIOA,3,2);  // alternative function mode
    GPIOA->AFR[0] &= ~(3 << (2*3));    // Clear out old alternative function  bits
  
    GPIOA->AFR[0] |= 1 << (4*3);    // Select alternative function 1

    // serial communication 
    //initADC();
    initSerial(9600); //115200
    //initTimer2();

    // EXTI4 interrupt for tach pulse counting on PB4
    RCC->APB2ENR |= (1 << 0); // enable SYSCFG

    SYSCFG->EXTICR[1] &= ~(7 << 0); // clear perhaps previously set bits
    SYSCFG->EXTICR[1] |= (1 << 0);  // map EXTI2 interrupt to PB4
    
    EXTI->FTSR1 |= (1 << 4); // select falling edge trigger for PB4 input
    EXTI->IMR1 |= (1 << 4);  // enable PB4 interrupt
    
    NVIC->ISER[0] |= (1 << 10); // IRQ 10 maps to EXTI4
    __enable_irq();

}

void delay(volatile uint32_t dly)
{
    while(dly--);
}

void enablePullUp(GPIO_TypeDef *Port, uint32_t BitNumber)
{
	Port->PUPDR = Port->PUPDR &~(3u << BitNumber*2); // clear pull-up resistor bits
	Port->PUPDR = Port->PUPDR | (1u << BitNumber*2); // set pull-up bit
}
void pinMode(GPIO_TypeDef *Port, uint32_t BitNumber, uint32_t Mode)
{
	/*
        Modes : 00 = input
                01 = output
                10 = special function
                11 = analog mode
	*/
	uint32_t mode_value = Port->MODER;
	Mode = Mode << (2 * BitNumber);
	mode_value = mode_value & ~(3u << (BitNumber * 2));
	mode_value = mode_value | Mode;
	Port->MODER = mode_value;
}
// delete timer 2 completely  or replace with 

void initTimer2(void)
{
    RCC->APB1ENR1 |= (1 << 0); // enable Timer 2
    TIM2->CR1 = 0;
    TIM2->CCMR2 = (0b110 << 12) + (1 << 11)+(1 << 10);
    TIM2->CCER |= (1 << 12);
    TIM2->ARR = 2000-1;
    TIM2->CCR4 = 500;
    TIM2->EGR |= (1 << 0);
    TIM2->CR1 = (1 << 7);
    TIM2->CR1 |= (1 << 0);  
}

void initClocks()
{
	// Initialize the clock system to a higher speed.
	// At boot time, the clock is derived from the MSI clock 
	// which defaults to 4MHz.  Will set it to 80MHz
	// See chapter 6 of the reference manual (RM0393)
	    RCC->CR &= ~(1 << 24); // Make sure PLL is off

	    RCC->PLLCFGR = (1 << 25) + (1 << 24) + (1 << 22) + (1 << 21) + (1 << 17) + (80 << 8) + (1 << 0);	
	    RCC->CR |= (1 << 24); // Turn PLL on
	    while( (RCC->CR & (1 << 25))== 0); // Wait for PLL to be ready
	// configure flash for 4 wait states (required at 80MHz)
	    FLASH->ACR &= ~((1 << 2)+ (1 << 1) + (1 << 0));
	    FLASH->ACR |= (1 << 2); 
	    RCC->CFGR |= (1 << 1)+(1 << 0); // Select PLL as system clock
}

// added from serial communication file
void initSerial(uint32_t baudrate)
{
    RCC->AHB2ENR |= (1 << 0); // make sure GPIOA is turned on
    pinMode(GPIOA,2,2); // alternate function mode for PA2
    selectAlternateFunction(GPIOA,2,7); // AF7 = USART2 TX
    pinMode(GPIOA,15,2); 
    selectAlternateFunction(GPIOA,15,3);
    RCC->APB1ENR1 |= (1 << 17); // turn on USART2

	const uint32_t CLOCK_SPEED=80000000;    
	uint32_t BaudRateDivisor;
	
	BaudRateDivisor = CLOCK_SPEED/baudrate;	
	USART2->CR1 = 0;
	USART2->CR2 = 0;
	USART2->CR3 = (1 << 12); // disable over-run errors
	USART2->BRR = BaudRateDivisor;
	USART2->CR1 =  (1 << 3);  // enable the transmitter and receiver
    USART2->CR1 |=  (1 << 2);  // enable the transmitter and receiver
	USART2->CR1 |= (1 << 0);
}
int _write(int file, char *data, int len)
{
    if ((file != STDOUT_FILENO) && (file != STDERR_FILENO))
    {
        errno = EBADF;
        return -1;
    }
    while(len--)
    {
        eputc(*data);    
        data++;
    }    
    return 0;
}
void eputc(char c)
{
    while( (USART2->ISR & (1 << 6))==0); // wait for ongoing transmission to finish
    USART2->TDR=c;
}       

void EXTI4_IRQHandler()
{
    //PIOB->BSRR = (1 << 3); // set PB3 to turn on LED 
    //GPIOB->IDR |= (1 << 4);
    EXTI->PR1 = (1 << 4);   // clear interrupt pending flag
    pulse_count++; //counting the pulse
    
}

// ------------------------------------------------------------
// TIM6 ISR - toggles PB6 to generate buzzer tone
// ------------------------------------------------------------
void TIM6_DAC_IRQHandler(void)
{
    TIM6->SR &= ~(1 << 0);             // clear update interrupt flag

    if (playing)
    {
        GPIOB->ODR ^= (1 << 6);        // toggle PB6 → 500Hz tone on buzzer
    }
    else
    {
        GPIOB->ODR &= ~(1 << 6);       // ensure buzzer pin is LOW when off
    }
}


void SysTick_Handler(void)
{    
   milliseconds++;
}
void delay_ms(unsigned dly){
    unsigned end = milliseconds + dly;
    while(milliseconds != end);
}
// I/O List for Encoder:
// PA0 : TIM2_CH1 (Alternative function 1) : Clock pin on encoder
// PB3 : TIM2_CH2 (Alternative function 1) : DT pin on encoder (quadrature type input)
// PB4 : GPIOA - SW input from encoder (maybe)
void initEncoder()
{
    RCC->AHB2ENR |= (1 << 0) | (1 << 1); // make sure GPIOA,B are turned on   
    pinMode(GPIOA,0,2);
    selectAlternateFunction(GPIOA,0,1);
    pinMode(GPIOB,3,2);
    selectAlternateFunction(GPIOB,3,1);
    //pinMode(GPIOB,4,0);
    RCC->APB1ENR1 |= (1 << 0); // enable Timer 2
    
    TIM2->CR1 = 0;
    TIM2->CNT = 0;

    TIM2->ARR = 159; // full range encoder count

    // Encoder mode 3 (TI1 + TI2)
    TIM2->SMCR = 3;

    // TI1 and TI2 as inputs
    TIM2->CCMR1 = 0;
    TIM2->CCMR1 |= (1 << 0);  // CC1S = input
    TIM2->CCMR1 |= (1 << 8);  // CC2S = input

    TIM2->CCER = 0;

    TIM2->CR1 |= 1;

}
