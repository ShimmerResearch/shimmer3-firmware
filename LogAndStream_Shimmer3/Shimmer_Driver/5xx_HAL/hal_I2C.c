/*
 * Adapted from Texas Instruments supplied example code
 */

/*******************************************************************************
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#include <stdint.h>
#include <string.h>

#include "hal_I2C.h"
#include "hal_TB0.h"
#include "msp430.h"

#include "../shimmer_definitions.h"

#define I2C_TIMEOUT \
  164 //164/32768 = 5.005ms
      //Longest I2C operations is reading 128bytes from
      //EEPROM on daughter card. This takes approx. 3.6ms
      //(from measurements)

#define I2C_TIMEOUT_TCXO              1280 //1280/255765.625 = 5ms

//UCB0 on P3.1 (SDA) and P3.2 (SCL); SW_I2C (P8.4) powers their pull-ups
#define I2C_PIN_SDA                   BIT1
#define I2C_PIN_SCL                   BIT2
#define I2C_PIN_PULLUPS               BIT4
//Half an SCL period while clearing the bus: 5us of the 24MHz MCLK, 100kHz
#define I2C_BUS_CLEAR_HALF_BIT_CYCLES 120
//A slave stops mid-byte with at most 8 data bits and an ACK left to clock
#define I2C_BUS_CLEAR_MAX_CLOCKS      9

//Bound on the polls of a START or STOP the USCI is sending. Each one takes
//tens of microseconds at 400kHz; these bounds are a few milliseconds.
#define I2C_STT_STP_SPIN_LIMIT        3000U //tight polls, in the ISR
#define I2C_PROBE_POLL_LIMIT          2000U //polls 1us apart, in the address scan
#define I2C_PROBE_POLL_CYCLES         24    //1us of the 24MHz MCLK

//Local Variables
//@brief tx_packet_length is how many bytes of data will be written
uint8_t tx_packet_length;
//@brief *tx_packet_data will point to the data to be written
uint8_t *tx_packet_data;

//@brief rx_packet_length is how many bytes of data will be read
uint16_t rx_packet_length;
//@brief *rx_packet_data will point to the data to be read
uint8_t *rx_packet_data;

//The flags and counters below are shared with USCI_I2C_ISR, so they are volatile.
//@brief receive flag is used for the ISR to know the MSP430 will be reading from the CP
volatile uint8_t receive_flag;
//@brief transmit flag is used for the ISR to know the MSP430 will be writing to the CP
volatile uint8_t transmit_flag;
//@brief set by the ISR when the slave didn't acknowledge, which ends the transfer
volatile uint8_t nack_flag;

//@brief Used to keep track how many bytes have been received
volatile uint16_t rx_byte_ctr;
//@brief Used to keep track how many bytes have been transmitted
volatile uint8_t tx_byte_ctr;

uint16_t i2c_timeout;

extern float clockFreq;

/* variables used in i2c address scanning (used for BMP180/280 check) */
uint8_t slave_addresses[128U + 2U];
uint8_t *slave_address_pointer;

/**
 * @brief Frees a bus that a slave is holding by keeping SDA low.
 *
 * A slave that stops mid-byte, because its transfer was abandoned by a USCI
 * reset or by an MCU reset (which doesn't power-cycle the sensors), keeps
 * driving SDA for a 0 bit or an ACK until it sees more clocks. Nothing else
 * will clock it: the USCI can't make a START while SDA is low, so every later
 * transfer, and the address scan at boot, waits on it.
 *
 * The standard recovery: with the USCI held in reset and the pull-ups on,
 * clock SCL as a GPIO until the slave lets SDA go, at most a byte and its ACK,
 * then make a STOP. The USCI must be in reset (UCSWRST) on entry.
 *
 * @return 1 if SDA is high afterwards, otherwise 0
 */
static uint8_t I2C_busClear(void)
{
  uint8_t clocks, sel, dir, out, sdaHigh;
  uint16_t gie;

  if (P3IN & I2C_PIN_SDA)
  {
    return 1; //Nothing holds the bus
  }
  if (!(P8OUT & I2C_PIN_PULLUPS))
  {
    return 0; //No pull-ups: the lines can't be released or read
  }

  gie = __get_SR_register() & GIE;
  __disable_interrupt();

  sel = P3SEL;
  dir = P3DIR;
  out = P3OUT;
  //Open drain by hand: the output latch is low, so P3DIR set drives the line
  //low and P3DIR clear releases it to the pull-up
  P3OUT &= ~(I2C_PIN_SDA + I2C_PIN_SCL);
  P3DIR &= ~(I2C_PIN_SDA + I2C_PIN_SCL);
  P3SEL &= ~(I2C_PIN_SDA + I2C_PIN_SCL);
  __delay_cycles(I2C_BUS_CLEAR_HALF_BIT_CYCLES);

  for (clocks = 0; clocks < I2C_BUS_CLEAR_MAX_CLOCKS && !(P3IN & I2C_PIN_SDA); clocks++)
  {
    P3DIR |= I2C_PIN_SCL; //SCL low
    __delay_cycles(I2C_BUS_CLEAR_HALF_BIT_CYCLES);
    P3DIR &= ~I2C_PIN_SCL; //SCL released
    __delay_cycles(I2C_BUS_CLEAR_HALF_BIT_CYCLES);
  }

  //STOP: SDA taken low while SCL is low, then SCL released, then SDA
  P3DIR |= I2C_PIN_SCL;
  __delay_cycles(I2C_BUS_CLEAR_HALF_BIT_CYCLES);
  P3DIR |= I2C_PIN_SDA;
  __delay_cycles(I2C_BUS_CLEAR_HALF_BIT_CYCLES);
  P3DIR &= ~I2C_PIN_SCL;
  __delay_cycles(I2C_BUS_CLEAR_HALF_BIT_CYCLES);
  P3DIR &= ~I2C_PIN_SDA;
  __delay_cycles(I2C_BUS_CLEAR_HALF_BIT_CYCLES);
  sdaHigh = (P3IN & I2C_PIN_SDA) ? 1 : 0;

  //Hand the pins back to the USCI first: it is in reset, so it leaves them
  //released, and they are never driven high
  P3SEL |= sel & (I2C_PIN_SDA + I2C_PIN_SCL);
  P3OUT = (P3OUT & ~(I2C_PIN_SDA + I2C_PIN_SCL)) | (out & (I2C_PIN_SDA + I2C_PIN_SCL));
  P3DIR = (P3DIR & ~(I2C_PIN_SDA + I2C_PIN_SCL)) | (dir & (I2C_PIN_SDA + I2C_PIN_SCL));

  __bis_SR_register(gie);
  return sdaHigh;
}

/**
 * @brief Abandons a transfer: puts the USCI in reset, drops any START or STOP
 * it still had pending, and clears the bus.
 *
 * UCSWRST alone clears UCB0IE, UCB0IFG and the status bits and leaves the
 * rest unchanged, so a pending UCTXSTT or UCTXSTP would still be set when the
 * USCI is released. The clock source is kept; the caller releases UCSWRST.
 */
static void I2C_abortTransfer(void)
{
  UCB0CTL1 = (UCB0CTL1 & UCSSEL_3) | UCSWRST;
  transmit_flag = 0;
  receive_flag = 0;
  I2C_busClear();
}

/**
 * @brief Arms the transfer timeout: TB0CCR4 as a one-shot compare, whose
 * interrupt (TIMER0_B1_ISR, main.c) wakes the LPM0 wait below.
 *
 * The wait tests its timeout only when something wakes it. The USCI interrupt
 * does while the transfer runs; once the bus stalls, nothing else may, because
 * the wait runs inside a task, and while a task is executing ShimTask_set()
 * doesn't wake the CPU for a newly queued one.
 */
static void I2C_timeoutArm(uint16_t start)
{
  TB0CCR4 = start + i2c_timeout;
  TB0CCTL4 = CCIE; //Compare mode, flag cleared
}

static void I2C_timeoutDisarm(void)
{
  TB0CCTL4 = 0;
}

/**
 * @brief <b>Function Name</b>:     : I2C_Master_Init
 * @brief  <b>Description</b>: Initializes the I2C Master Block. Upon successful
 * initialization of the I2C master block, this function will have set the bus
 * speed for the master, and will have enabled the SPI Master block.
 * @param Input Parameters:
 * <BR> uint8_t <b>selectClockSource</b> selects Clock source SMCLK (0x00)
 * or ACLK (0x1)
 * <BR> uint32_t <b>clockSourceFrequency</b> is the frequency of the
 * selected clock source
 * <BR> uint32_t <b>desiredI2CClock</b> is the desired clock rate for SPI
 * communication
 * @return Return Values: None
 **/
void I2C_Master_Init(uint8_t selectClockSource, uint32_t clockSourceFrequency, uint32_t desiredI2CClock)
{
  //Disable the USCI Module, with nothing left pending from an earlier transfer
  UCB0CTL1 = UCSWRST;

  //Free the bus if a slave is still holding it from a transfer that was cut
  //short, so the address scan at boot and the first transfers can start
  I2C_busClear();

  //Clock Source
  if (S_MCLK == selectClockSource)
  {
    UCB0CTL1 |= UCSSEL_2; //Select SMCLK
  }
  else
  {
    //Select ACLK
    UCB0CTL1 |= UCSSEL_1;
  }

  //Set the Baud rate
  UCB0BR0 = (unsigned int) (clockSourceFrequency / desiredI2CClock);
  UCB0BR1 = (unsigned int) ((clockSourceFrequency / desiredI2CClock) >> 8);

  /*!
   * Configure as I2C master mode.
   * UCMST = Master mode
   * UCMODE_3 = I2C mode
   * UCSYNC = Synchronous mode
   */
  UCB0CTL0 |= UCMST + UCMODE_3 + UCSYNC;

  if (clockFreq == (float) MSP430_CLOCK)
  {
    i2c_timeout = I2C_TIMEOUT;
  }
  else
  {
    i2c_timeout = I2C_TIMEOUT_TCXO;
  }

  I2C_Enable();
}

/**
 * @brief <b>Function Name</b>:     : I2C_Set_Slave_Address
 * @brief  <b>Description</b>: This function will set the address that the I2C
 * Master will place on the bus when initiating a transaction.
 * @param Input Parameters:
 * <BR> uint8_t <b>slaveAddress</b> is the address of the slave
 * @return Return Values: None
 **/
void I2C_Set_Slave_Address(uint8_t slaveAddress)
{
  //Set the address of the slave with which the master will communicate.
  UCB0I2CSA = slaveAddress;
}

/**
 * @brief <b>Function Name</b>:     : I2C_Enable
 * @brief  <b>Description</b>: Enable the I2C block
 * @param Input Parameters: None
 * @return Return Values: None
 **/
void I2C_Enable(void)
{
  //Reset the UCSWRST bit to enable the USCI Module
  UCB0CTL1 &= ~(UCSWRST);
}

/**
 * @brief <b>Function Name</b>:     : I2C_Disable
 * @brief  <b>Description</b>: Disable the I2C block
 * @param Input Parameters: None
 * @return Return Values: None
 **/
void I2C_Disable(void)
{
  //Set the UCSWRST bit to disable the USCI Module
  UCB0CTL1 |= (UCSWRST);
}

/**
 * @brief <b>Function Name</b>:     : I2C_Set_Mode
 * @brief  <b>Description</b>: When the receive parameter is set to 0x01, the
 * address will indicate that the I2C module is in receive mode; otherwise, the
 * I2C module is in transmit mode
 * @param Input Parameters:
 * <BR> uint16_t <b>receive</b> can be set to a 0x01 to be in receive mode,
 *  or 0x00 to be in transmit mode
 * @return Return Values: None
 **/
void I2C_Set_Mode(uint16_t receive)
{
  //Disable the USCI module
  UCB0CTL1 |= UCSWRST;

  if (receive)
  {
    //Configure in receive mode
    UCB0CTL1 &= ~(UCTR);
  }
  else
  {
    //Configure in transmit mode
    UCB0CTL1 |= UCTR;
  }
}

/**
 * @brief <b>Function Name</b>:     : I2C_Bus_Busy
 * @brief  <b>Description</b>: This function returns an indication of whether or
 * not the I2C bus is busy. This function can be used in a multi-master
 * enviroment to determine if another master is currently using the bus. This
 * function checks the status of the bus via UCBBUSY bit in the UCB0STAT register.
 * @param Input Parameters: None
 * @return Return Values:
 * <BR>Returns 0x01 if the I2C master is busy; otherwise, returns 0x00.
 **/
uint16_t I2C_Bus_Busy(void)
{
  //Return the bus busy status.
  if (UCB0STAT & UCBBUSY)
  {
    return (RET_OK);
  }
  else
  {
    return (RET_ERR);
  }
}

/**
 * @brief <b>Function Name</b>:     : I2C_Busy
 * @brief  <b>Description</b>: This function returns an indication of whether or
 * not the I2C module is busy transmitting or receiving data. This function
 * checks if the transmit or receive flag is set.
 * @param Input Parameters: None
 * @return Return Values:
 * <BR>Returns 0x01 if the I2C master is busy; otherwise, returns 0x00.
 **/
uint16_t I2C_Busy(void)
{
  //Return the busy status.
  if ((UCB0IFG & UCB0TXIFG) || (UCB0IFG & UCB0RXIFG))
  {
    return ((uint16_t) RET_OK);
  }
  else
  {
    return ((uint16_t) RET_ERR);
  }
}

/**
 * @brief <b>Function Name</b>:     : I2C_Interrupt_Enable
 * @brief  <b>Description</b>: Enables Individual I2C interrupts
 * @param Input Parameters:
 * <BR> uint8_t <b>interruptFlags</b> is the logical OR of any of the
 * following:
 *  <BR> I2C_INT_STOP - STOP condition interrupt
 *  <BR> I2C_INT_START - START condition interrupt
 *  <BR> I2C_INT_DATA_RX -Receive interrupt
 *  <BR> I2C_INT_DATA_TX - Transmit interrupt
 *  <BR> I2C_INT_NACK - Not-acknowledge interrupt
 *  <BR> I2C_INT_CALIFG - Arbitration lost interrupt
 * @return Return Values: None
 **/
void I2C_Interrupt_Enable(uint8_t interruptFlags)
{
  //Enable the interrupt masked bit
  UCB0IE |= interruptFlags;
}

/**
 * @brief <b>Function Name</b>:     : I2C_Interrupt_Disable
 * @brief  <b>Description</b>: Disables Individual I2C interrupts
 * @param Input Parameters:
 * <BR> uint8_t <b>interruptFlags</b> is the logical OR of any of the
 * following:
 *  <BR> I2C_INT_STOP - STOP condition interrupt
 *  <BR> I2C_INT_START - START condition interrupt
 *  <BR> I2C_INT_DATA_RX -Receive interrupt
 *  <BR> I2C_INT_DATA_TX - Transmit interrupt
 *  <BR> I2C_INT_NACK - Not-acknowledge interrupt
 *  <BR> I2C_INT_CALIFG - Arbitration lost interrupt
 * @return Return Values: None
 **/
void I2C_Interrupt_Disable(uint8_t interruptFlags)
{
  //Disable the interrupt masked bit
  UCB0IE &= ~(interruptFlags);
}

/**
 * @brief <b>Function Name</b>:     : I2C_Interrupt_Clear
 * @brief  <b>Description</b>: Clears Individual I2C interrupts flags
 * @param Input Parameters:
 * <BR> uint8_t <b>interruptFlags</b> is the logical OR of any of the
 * following:
 *  <BR> I2C_INT_STOP - STOP condition interrupt
 *  <BR> I2C_INT_START - START condition interrupt
 *  <BR> I2C_INT_DATA_RX -Receive interrupt
 *  <BR> I2C_INT_DATA_TX - Transmit interrupt
 *  <BR> I2C_INT_NACK - Not-acknowledge interrupt
 *  <BR> I2C_INT_CALIFG - Arbitration lost interrupt
 * @return Return Values: None
 **/
void I2C_Interrupt_Clear(uint8_t interruptFlags)
{
  //Clear the I2C interrupt source.
  UCB0IFG &= ~(interruptFlags);
}

/**
 * @brief <b>Function Name</b>:     : I2C_Interrupt_Status
 * @brief  <b>Description</b>: Returns the interrupt status for the I2C module
 * based on which flag is passed.
 * @param Input Parameters:
 * <BR> uint8_t <b>mask</b> is the masked interrupt flag status to be
 * returned.
 *  <BR> I2C_INT_STOP - STOP condition interrupt
 *  <BR> I2C_INT_START - START condition interrupt
 *  <BR> I2C_INT_DATA_RX -Receive interrupt
 *  <BR> I2C_INT_DATA_TX - Transmit interrupt
 *  <BR> I2C_INT_NACK - Not-acknowledge interrupt
 *  <BR> I2C_INT_CALIFG - Arbitration lost interrupt
 * @return Return Values:
 * <BR>The current interrupt status, returned as 0x01 if active, otherwise 0x00.
 **/
uint8_t I2C_Interrupt_Status(uint8_t mask)
{
  //Return the interrupt status of the request masked bit.
  return (UCB0IFG & mask);
}

uint8_t TI_USCI_I2C_slave_present(uint8_t slave_address)
{
  uint8_t ucie, slave_add, returnValue;
  uint16_t polls = 0;
  ucie = UCB0IE;                //restore old UCB0I2CIE
  slave_add = UCB0I2CSA;        //store old slave address
  UCB0IE &= ~UCNACKIE;          //no NACK interrupt
  UCB0I2CSA = slave_address;    //set slave address
  UCB0IE &= ~(UCTXIE + UCRXIE); //no RX or TX interrupts

  uint16_t gie = __get_SR_register() & GIE; //Store current GIE state
  __disable_interrupt();

  UCB0CTL1 |= UCTR + UCTXSTT + UCTXSTP; //I2C TX, start condition
  //Wait for the STOP condition. A bus held low lets neither the START nor the
  //STOP out, so the wait is bounded: this runs at boot with interrupts off and
  //the watchdog held, where an unbounded wait never ends
  while ((UCB0CTL1 & UCTXSTP) && (polls < I2C_PROBE_POLL_LIMIT))
  {
    __delay_cycles(I2C_PROBE_POLL_CYCLES);
    polls++;
  }
  if (UCB0CTL1 & UCTXSTP)
  {
    //The address never went out: count it absent and free the bus for the
    //next one
    I2C_abortTransfer();
    I2C_Enable();
    returnValue = 0;
  }
  else
  {
    returnValue = !(UCB0IFG & UCNACKIFG);
  }

  __bis_SR_register(gie); //Restore original GIE state

  UCB0I2CSA = slave_add; //restore old slave address
  UCB0IE = ucie;         //restore old UCB0CTL1

  return returnValue; //return whether or not
  //a NACK occured
}

/**
 * @brief <b>Function Name</b>:     : I2C_Write_Packet_To_Sensor
 * @brief  <b>Description</b>: Sends data packet to sensor
 * @param Input Parameters:
 * <BR> uint8_t <b>writeData</b> is the pointer to the array which holds the
 * the packet to send to the sensor. writeData[0] = register the packet will
 * be written to. This function requires the user to have used I2C_Set_Slave_Address(),
 * to set the address of the sensor.
 * <BR> uint8_t <b>dataLength</b> is the number of data bytes to be written.
 * @return Return Values:
 * <BR>Returns 0x01 if successful; otherwise, returns 0x00.
 **/
uint8_t I2C_Write_Packet_To_Sensor(uint8_t *writeData, uint8_t dataLength)
{
  uint16_t start, now;
  uint16_t gie = __get_SR_register() & GIE; //Store current GIE state

  //Assign values to local variables
  tx_packet_length = dataLength;
  tx_packet_data = writeData;

  //Reset variables for transmission
  transmit_flag = 1;
  receive_flag = 0;
  nack_flag = 0;
  tx_byte_ctr = 0;
  rx_byte_ctr = 0;

  //Enable TX Interrupt to send write address, register and data
  __bic_SR_register(GIE);
  I2C_Interrupt_Enable(UCB0TXIE);
  I2C_Interrupt_Enable(UCNACKIE);
  UCB0IFG |= UCB0TXIFG;

  start = GetTB0();
  I2C_timeoutArm(start);
  //Enter Low Power Mode 0. The flag is only tested with interrupts disabled,
  //so the ISR can't clear it between the test and entering LPM0, which
  //re-enables them in the same instruction.
  while (transmit_flag == 1)
  {
    __bis_SR_register(LPM0_bits + GIE); //CPU off, enable interrupts
    __disable_interrupt();
    if (transmit_flag)
    {
      //only check for timeout if still waiting to transmit. Elapsed ticks,
      //so a wrap of TB0 between start and now doesn't matter
      now = GetTB0();
      if ((uint16_t) (now - start) >= i2c_timeout)
      {
        //timeout. Reset the USCI before restoring GIE: in I2C mode UCSWRST
        //also clears UCB0IE and UCB0IFG, so no USCI_I2C_ISR can run against
        //the abandoned transfer once interrupts are back on.
        I2C_timeoutDisarm();
        I2C_abortTransfer();
        __bis_SR_register(gie); //Restore original GIE state
        __delay_cycles(240000); //10ms (assuming 24MHz clock)
        I2C_Enable();
        return 0;
      }
    }
  }
  I2C_timeoutDisarm();
  __bis_SR_register(gie); //Restore original GIE state
  return nack_flag ? 0 : 1;
}

/**
 * @brief <b>Function Name</b>:     : I2C_Read_Packet_From_Sensor
 * @brief  <b>Description</b>: Sends data packet to Authentication CP
 * @param Input Parameters:
 * <BR> uint8_t <b>readData</b> is the pointer to the array which holds the
 * the packet to send to the sensor. readData[0] = register the packet will
 * be read from. This function requires the user to have used I2C_Set_Slave_Address(),
 * to set the address of the sensor.
 * <BR> uint8_t * <b>readData</b> is the number of data bytes to be read.
 * @return Return Values:
 * <BR>Returns 0x01 if successful; otherwise, returns 0x00.
 **/
uint8_t I2C_Read_Packet_From_Sensor(uint8_t *readData, uint16_t dataLength)
{
  uint16_t start, now;
  uint16_t gie = __get_SR_register() & GIE; //Store current GIE state

  //Assign values to local variables
  rx_packet_length = dataLength;
  rx_packet_data = readData;

  //Reset variables for transmission
  transmit_flag = 0;
  receive_flag = 1;
  nack_flag = 0;
  tx_byte_ctr = 0;
  rx_byte_ctr = 0;

  //Enable TX Interrupt to send write address, and register
  __bic_SR_register(GIE);

  I2C_Interrupt_Enable(UCB0TXIE);
  I2C_Interrupt_Enable(UCNACKIE);
  UCB0IFG |= UCB0TXIFG;

  start = GetTB0();
  I2C_timeoutArm(start);
  //Enter Low Power Mode 0. The flag is only tested with interrupts disabled,
  //so the ISR can't clear it between the test and entering LPM0, which
  //re-enables them in the same instruction.
  while (receive_flag == 1)
  {
    __bis_SR_register(LPM0_bits + GIE); //CPU off, enable interrupts
    __disable_interrupt();
    if (receive_flag)
    {
      //only check for timeout if still waiting to transmit. Elapsed ticks,
      //so a wrap of TB0 between start and now doesn't matter
      now = GetTB0();
      if ((uint16_t) (now - start) >= i2c_timeout)
      {
        //timeout. Reset the USCI before restoring GIE: in I2C mode UCSWRST
        //also clears UCB0IE and UCB0IFG, so no USCI_I2C_ISR can run against
        //the abandoned transfer once interrupts are back on.
        I2C_timeoutDisarm();
        I2C_abortTransfer();
        __bis_SR_register(gie); //Restore original GIE state
        __delay_cycles(240000); //10ms (assuming 24MHz clock)
        I2C_Enable();
        return 0;
      }
    }
  }
  I2C_timeoutDisarm();
  __bis_SR_register(gie); //Restore original GIE state
  return nack_flag ? 0 : 1;
}

char i2cSlavePresent(char address)
{
  /* Approach relies on i2cSlaveDiscover having been performed before this */
  uint8_t i;
  for (i = 0; i < sizeof(slave_addresses) - 1; i++)
  {
    if (slave_addresses[i] == 0xFF && slave_addresses[i + 1U] == 0xFE)
    {
      break;
    }

    if (slave_addresses[i] == address)
    {
      return 1;
    }
  }
  return 0;
}

void i2cSlaveDiscover(void)
{
  char address;

  memset(slave_addresses, 0x00, sizeof(slave_addresses) / sizeof(slave_addresses[0]));

  slave_address_pointer = &slave_addresses[0];
  for (address = 1; address < 127; address++)
  {
    if (TI_USCI_I2C_slave_present(address))
    {
      *slave_address_pointer++ = address;
    }
  }
  *slave_address_pointer++ = 0xFF;
  *slave_address_pointer = 0xFE;
}

#pragma vector = USCI_B0_VECTOR

__interrupt void USCI_I2C_ISR(void)
{
  uint16_t spins;

  switch (__even_in_range(UCB0IV, 12))
  {
    case 0:
      break; //Vector  0: No interrupts
    case 2:
      break; //Vector  2: ALIFG
    case 4:  //Vector  4: NACKIFG
      //Nothing acknowledged the address or a byte. End the transfer with a
      //STOP and report it failed. Re-STARTing instead repeats the same NACK
      //until the caller's timeout resets the USCI mid-transfer, and that can
      //leave a slave holding SDA low.
      UCB0CTL1 |= UCTXSTP;
      UCB0IE &= ~(UCB0TXIE + UCB0RXIE + UCNACKIE);
      UCB0IFG &= ~UCB0TXIFG;
      nack_flag = 1;
      transmit_flag = 0;
      receive_flag = 0;
      break;
    case 6:
      break; //Vector  6: STTIFG
    case 8:
      break; //Vector  8: STPIFG
    case 10: //Vector 10: RXIFG
      if (rx_byte_ctr < rx_packet_length - 1)
      {
        *rx_packet_data++ = UCB0RXBUF; //Read receive buffer
        rx_byte_ctr++;
        if (rx_byte_ctr == rx_packet_length - 1)
        {
          UCB0CTL1 |= UCTXSTP; //Send I2C stop condition
        }
      }
      else
      {
        //Clear RX Flag
        receive_flag = 0;
        *rx_packet_data = UCB0RXBUF;
        I2C_Interrupt_Disable(UCNACKIE);
        I2C_Interrupt_Disable(UCB0RXIE);
        //__bic_SR_register_on_exit(LPM0_bits);  // Exit active CPU
      }
      break;
    case 12: //Vector 12: TXIFG
      if (transmit_flag)
      {
        if (tx_byte_ctr == 0)
        {
          //Ensure stop condition got sent. Bounded: if the bus is held it
          //never goes, and the caller's timeout recovers the bus
          spins = I2C_STT_STP_SPIN_LIMIT;
          while ((UCB0CTL1 & UCTXSTP) && --spins)
            ;
          UCB0CTL1 |= UCTR + UCTXSTT; //Send I2C start condition
          tx_byte_ctr++;
        }
        else if (tx_packet_length >= tx_byte_ctr)
        {
          UCB0TXBUF = *tx_packet_data++; //Send data bytes
          tx_byte_ctr++;
        }
        else
        {
          UCB0CTL1 |= UCTXSTP; //Send I2C stop condition
          I2C_Interrupt_Disable(UCB0TXIE);
          I2C_Interrupt_Disable(UCNACKIE);
          transmit_flag = 0;
          //__bic_SR_register_on_exit(LPM0_bits);  // Exit active CPU
        }
      }
      else if (receive_flag)
      {
        if (tx_byte_ctr == 0)
        {
          //Ensure stop condition got sent, bounded as above
          spins = I2C_STT_STP_SPIN_LIMIT;
          while ((UCB0CTL1 & UCTXSTP) && --spins)
            ;
          UCB0CTL1 |= UCTR + UCTXSTT; //I2C start condition
          tx_byte_ctr++;
        }
        else if (tx_byte_ctr == 1)
        {
          UCB0TXBUF = rx_packet_data[0]; //Send Register name
          tx_byte_ctr++;
        }
        else if (tx_byte_ctr == 2)
        {
          UCB0CTL1 &= ~(UCTR); //Enable receive bit
          UCB0CTL1 |= UCTXSTT; //Send I2C start condition
          //In the case of reading one byte, poll on start condition bit
          if (rx_packet_length == 1)
          {
            //Wait for start condition to be transmitted, bounded as above
            spins = I2C_STT_STP_SPIN_LIMIT;
            while ((UCB0CTL1 & UCTXSTT) && --spins)
              ;
            UCB0CTL1 |= UCTXSTP; //Send I2C stop condition
          }
          I2C_Interrupt_Enable(UCB0RXIE);  //Enable RX interrupt
          I2C_Interrupt_Disable(UCB0TXIE); //Disable TX interrupt
          tx_byte_ctr++;
        }
      }
      break;
    default:
      break;
  }
  __bic_SR_register_on_exit(LPM0_bits); //Exit active CPU
}
