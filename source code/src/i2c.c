#include <stm32l432xx.h>
#include "i2c.h"
#include "eeng1030_lib.h"
void delay(volatile uint32_t dly);
void initI2C(void)
{
    pinMode(GPIOB,3,1); // make PB3 (internal LED) an output
    pinMode(GPIOB,6,2); // alternative functions for PB6 and PB7
    pinMode(GPIOB,7,2); 
    selectAlternateFunction(GPIOB,6,4); // Alternative function 4 = I2C1_SCL
    selectAlternateFunction(GPIOB,7,4); // Alternative function 4 = I2C1_SDA
    RCC->APB1ENR1 |= (1 << 21); // enable I2C1
    I2C1->CR1 = 0;
    delay(100);
	// 80MHz/(7+1)=10MHz.  10MHz/100kHz=100.  
	// So need (49+1) clock ticks for high part of clock + (49+1) for low part
	// Data setup delays are also added (Table 138 in the ref. manual was a bit of a help)
    I2C1->TIMINGR = (7 << 28) + (4 << 20) + (2 << 16) + (49 << 8) + (49); 
    I2C1->ICR |= 0xffff; // clear all pending interrupts
    I2C1->CR1 |= (1 << 0);
}
void ResetI2C()
{
	I2C1->CR1 &= ~(1 << 0);
	while(I2C1->CR1 & (1 << 0)); // forces a wait.
	I2C1->CR1 = (1 << 0);
}
void I2CStart(uint8_t address, int rw, int nbytes)
{
	unsigned timeout;
	unsigned Reg;
	Reg = I2C1->CR2;
	Reg &= ~(1 << 13); // clear START bit
	// Clear out old address.
	Reg &= ~(0x3ff); // clear out lower 10 bits of CR1 (address)
	Reg |= ((address<<1) & 0x3ff); // set address bits
	if (rw==READ)
		Reg |= (1 << 10); // read mode so set read bit
	else
		Reg &= ~(1 << 10); // write mode
	Reg &= ~(0x00ff0000); // clear byte count
	Reg |= (nbytes << 16); // set byte count
	//Reg |= (1 << 24); // set reload bit
	Reg |= (1 << 13); // set START bit
	I2C1->CR2 = Reg;
	while((I2C1->ISR & (1 << 0))==0); // wait for transmit complete
}
void I2CReStart(uint8_t address, int rw, int nbytes)
{	
	unsigned Reg;
	Reg = I2C1->CR2;
	Reg &= ~(1<<13); // clear START bit
	// Clear out old address.
	Reg &= ~(0x3ff); // clear out lower 10 bits of CR1 (address)
	Reg |= ((address<<1) & 0x3ff); // set address bits
	if (rw==READ)
		Reg |= (1 << 10); // read mode so set read bit
	else
		Reg &= ~(1 << 10); // write mode
	Reg &= ~(0x00ff0000); // clear byte count
	Reg |= (nbytes << 16); // set byte count
	Reg &= ~(1 << 24); // clear reload bit
	Reg |= (1 << 13); // set START bit
	I2C1->CR2 = Reg;	
	while((I2C1->ISR & (1 << 0))==0); // wait for transmit complete
}
void I2CStop()
{
	I2C1->CR2 &= ~(1 << 24); // clear reload bit
	delay(10);
	I2C1->CR2 |= (1 << 14);	// set stop bit
	while((I2C1->ISR & (1 << 0))==0); // wait for transmit complete

}

void I2CWrite(uint8_t Data)
{

	while((I2C1->ISR & (1 << 0))==0); // wait for transmit complete
	I2C1->TXDR = Data;	
	while((I2C1->ISR & (1 << 0))==0); // wait for transmit complete
	
	
}
uint8_t I2CRead()
{
	while((I2C1->ISR & (1 << 2))==0); // wait for receive complete
	return I2C1->RXDR; 		// return rx data
}