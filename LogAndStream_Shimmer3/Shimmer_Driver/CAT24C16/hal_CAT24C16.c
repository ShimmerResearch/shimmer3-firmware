/*
 * hal_cat24c16.c
 *
 * Shimmer3's side of the CAT24C16 EEPROM: power, the I2C bus, and the bus
 * callbacks the shared driver (log-and-stream-common EEPROM/cat24c16.c) reaches
 * the part through.
 *
 *  Created on: 31 Jul 2025
 *      Author: MarkNolan
 */

#include "hal_CAT24C16.h"

#include <stdint.h>

#include "../5xx_HAL/hal_I2C.h"
#include "EEPROM/cat24c16.h"
#include "hal_Board.h"
#include "log_and_stream_globals.h"
#include "msp430.h"

extern void BlinkTimerStart(void);
extern void BlinkTimerStop(void);
extern void I2C_start(uint8_t controlExpBrd);
extern void I2C_stop(uint8_t controlExpBrd);

static int32_t eepromBusWrite(void *handle, uint8_t devAddr, uint8_t *buf, uint16_t len);
static int32_t
eepromBusRead(void *handle, uint8_t devAddr, uint8_t wordAddr, uint8_t *outBuffer, uint16_t len);
static void eepromDelayMs(uint32_t ms);

//One part, on the one I2C bus, so no handle
static cat24c16dev_ctx_t eepromCtx = { eepromBusWrite, eepromBusRead, eepromDelayMs, 0 };

//I2C pull-ups must be enabled (on rev4 boards) before initialising the I2C bus
static void eepromPowerOn(void)
{
  I2C_start(1);
  //Source from SMCLK, which is running @ 24MHz. 400kHz desired BRCLK
  I2C_Master_Init(S_MCLK, 24000000, 400000);
}

static void eepromPowerOff(void)
{
  I2C_stop(1);
}

uint8_t eepromTest(void)
{
  return CAT24C16_test(&eepromCtx);
}

void eepromRead(uint16_t dataAddr, uint16_t dataSize, uint8_t *dataBuf)
{
  eepromReadWrite(dataAddr, dataSize, dataBuf, EEPROM_READ);
}

void eepromWrite(uint16_t dataAddr, uint16_t dataSize, uint8_t *dataBuf)
{
  eepromReadWrite(dataAddr, dataSize, dataBuf, EEPROM_WRITE);
}

void eepromReadWrite(uint16_t dataAddr, uint16_t dataSize, uint8_t *dataBuf, enum EEPROM_RW eepromRW)
{
  uint8_t timer_was_stopped = 0;
  uint8_t wasI2cRunning = shimmerStatus.pinPvI2c;
  uint8_t wasExpBoardRunning = shimmerStatus.pinPvExt;

  //Spool up EEPROM and required timing peripherals
  if (TB0CTL == MC_0) //Timer is stopped
  {
    BlinkTimerStart();
    timer_was_stopped = 1;
  }

  eepromPowerOn();

  //EEPROM needs to be updated with latest bt baud rate, configure here
  if (eepromRW == EEPROM_READ)
  {
    CAT24C16_read(&eepromCtx, dataAddr, dataSize, dataBuf);
  }
  else
  {
    //Returns with the last page's write cycle over, so the power can go at once
    CAT24C16_write(&eepromCtx, dataAddr, dataSize, dataBuf);
  }

  //Wind down EEPROM and required timing peripherals
  if (!wasI2cRunning)
  {
    eepromPowerOff(); //turn OFF if it was not already ON
  }
  else if (!wasExpBoardRunning)
  {
    Board_setExpansionBrdPower(0); //turn OFF if it was not already ON
  }
  if (timer_was_stopped)
  {
    BlinkTimerStop();
  }
}

static int32_t eepromBusWrite(void *handle, uint8_t devAddr, uint8_t *buf, uint16_t len)
{
  (void) handle;
  I2C_Set_Slave_Address(devAddr);
  //At most a page and its word address, 17 bytes
  return I2C_Write_Packet_To_Sensor(buf, (uint8_t) len) ? 0 : 1;
}

static int32_t
eepromBusRead(void *handle, uint8_t devAddr, uint8_t wordAddr, uint8_t *outBuffer, uint16_t len)
{
  (void) handle;
  I2C_Set_Slave_Address(devAddr);
  //The HAL sends the buffer's first byte as the word address, then reads over it
  outBuffer[0] = wordAddr;
  return I2C_Read_Packet_From_Sensor(outBuffer, len) ? 0 : 1;
}

static void eepromDelayMs(uint32_t ms)
{
  while (ms--)
  {
    __delay_cycles(24000); //1 ms of the 24 MHz MCLK
  }
}
