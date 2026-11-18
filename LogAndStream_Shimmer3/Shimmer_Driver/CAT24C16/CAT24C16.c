/*
 * cat24c16.c
 *
 *  Created on: 21 Nov 2013
 *      Author: WeiboP
 */

#include "CAT24C16.h"

#include "../5xx_HAL/hal_Board.h"
#include "../5xx_HAL/hal_I2C.h"
#include "../5xx_HAL/hal_RTC.h"
#include "msp430.h"
#include "string.h"

#define min(a, b)                   ((a < b) ? a : b)

//Write cycle time, tWR: 5ms max (CAT24C16 datasheet, A.C. Characteristics).
//The device does not acknowledge its address until a write cycle has ended.
//120000 cycles of the 24MHz MCLK is 5ms.
#define CAT24C16_WAIT_WRITE_CYCLE() __delay_cycles(120000)

extern void I2C_start(uint8_t controlExpBrd);
extern void I2C_stop(uint8_t controlExpBrd);

void CAT24C16_init(void)
{
  CAT24C16_powerOn(); //I2C pull-ups must be enabled (on rev4 boards) before initialising I2C bus
  I2C_Master_Init(S_MCLK, 24000000,
      400000); //Source from SMCLK, which is running @ 24MHz. 400kHz desired BRCLK
}

void CAT24C16_powerOn(void)
{
  I2C_start(1);
}

void CAT24C16_powerOff(void)
{
  I2C_stop(1);
}

int32_t CAT24C16_read(uint16_t address, uint16_t length, uint8_t *outBuffer)
{
  if ((!length) || (length > CAT24C16_TOTAL_SIZE) || (address + length > CAT24C16_TOTAL_SIZE))
  {
    return CAT24C16_OUT_OF_BOUNDS_ERROR;
  }

  I2C_Set_Slave_Address(CAT24C16_ADDR | (address >> 8));
  *outBuffer = (uint8_t) (address & 0xFF);

  if (!I2C_Read_Packet_From_Sensor(outBuffer, length))
  {
    //read failed, set buffer to all 0xFF
    while (length--)
    {
      *(outBuffer++) = 0xFF;
    }
    return 1;
  }
  return 0;
}

//testing features:
//1 bytes writing: 0.087 ms
//16 bytes writing: 0.5 ms (within one page)
//16 bytes writing: 5.7 ms (cross pages)
//waiting time between two page writes: 5 ms (as suggested by the CAT24 user
//manual) 1024 bytes writing: 351 ms
int32_t CAT24C16_write(uint16_t address, uint16_t length, uint8_t *data)
{
  if ((!length) || (length > CAT24C16_TOTAL_SIZE) || (address + length > CAT24C16_TOTAL_SIZE))
  {
    return CAT24C16_OUT_OF_BOUNDS_ERROR;
  }

  int32_t result = 0;
  uint8_t final_addr;
  uint8_t buf[17], margin, this_write, inc_addr; //17 = PAGE_SIZE+1
  uint16_t mem_ptr, buf_offset;

  final_addr = CAT24C16_ADDR | (address >> 8);
  I2C_Set_Slave_Address(final_addr);

  inc_addr = 0;
  buf_offset = 0;
  mem_ptr = address;
  this_write = 0;

  while (mem_ptr < address + length)
  {
    //reassign I2C slave address
    if (inc_addr)
    {
      inc_addr = 0;
      I2C_Set_Slave_Address(++final_addr);
    }

    //The previous write cycle, this call's or an earlier one's, must have
    //ended before the device acknowledges this page
    CAT24C16_WAIT_WRITE_CYCLE();
    //bounds check
    margin = CAT24C16_PAGE_SIZE - (mem_ptr % CAT24C16_PAGE_SIZE);
    this_write = min(margin, address + length - mem_ptr);

    //set byte address
    buf[0] = mem_ptr & 0xff;
    //data copy
    memcpy(buf + 1, data + buf_offset, this_write);

    result = I2C_Write_Packet_To_Sensor(buf, this_write + 1) ? 0 : 1;
    if (result)
    {
      break;
    }

    mem_ptr += this_write;
    buf_offset += this_write;

    //if reaches edge of flash mem, reassign the slave address
    if (!(mem_ptr % CAT24C16_BLOCK_SIZE))
    {
      inc_addr = 1;
    }
  }
  return result;
}

//Write the backed-up bytes back over the test region and read them back.
//Returns 0 once the region holds the backup again, 1 if every try failed.
static uint8_t CAT24C16_restoreTestRegion(uint8_t *backup, uint8_t *readBack)
{
  uint8_t tries;

  for (tries = 0; tries < CAT24C16_TEST_RESTORE_TRIES; tries++)
  {
    //A write cycle may still be running here: from a test write that failed
    //after its data landed, or from the previous try. The device does not
    //acknowledge its address until that cycle ends, so the restore's first
    //page would be refused. CAT24C16_write() waits before each page it writes,
    //which covers that.
    if (CAT24C16_write(CAT24C16_TEST_OFFSET, CAT24C16_TEST_SIZE, backup))
    {
      continue;
    }

    //It does not wait after the last page, so the read-back does
    CAT24C16_WAIT_WRITE_CYCLE();
    if (CAT24C16_read(CAT24C16_TEST_OFFSET, CAT24C16_TEST_SIZE, readBack) == 0
        && memcmp(backup, readBack, CAT24C16_TEST_SIZE) == 0)
    {
      return 0;
    }
  }

  return 1;
}

uint8_t CAT24C16_test(void)
{
  uint8_t testResult = EEPROM_TEST_PASS;
  uint16_t i, j = 0;
  uint8_t test_eeprom_backup[CAT24C16_TEST_SIZE];
  uint8_t test_eeprom_wr[CAT24C16_TEST_SIZE];
  uint8_t test_eeprom_rd[CAT24C16_TEST_SIZE];

  if (CAT24C16_read(CAT24C16_TEST_OFFSET, CAT24C16_TEST_SIZE, test_eeprom_backup))
  {
    //Nothing has been written yet, so there is nothing to put back
    return EEPROM_TEST_FAIL_INITIAL_BACKUP;
  }

  while (testResult == EEPROM_TEST_PASS && j++ < 3)
  {
    //SRAND/RAND not working on MSP430 for some reason
    //srand((unsigned) RTC_get32());
    //for (i = 0; i < CAT24C16_TEST_SIZE; i++)
    //{
    //    test_eeprom_wr[i] = rand();
    //}

    //Increment value in. Each pass's pattern differs from the last, so a pass
    //whose write never happened cannot read back correctly.
    for (i = 0; i < CAT24C16_TEST_SIZE; i++)
    {
      test_eeprom_wr[i] = (uint8_t) (j + i);
    }

    memset(test_eeprom_rd, 0, CAT24C16_TEST_SIZE);
    if (CAT24C16_write(CAT24C16_TEST_OFFSET, CAT24C16_TEST_SIZE, test_eeprom_wr))
    {
      testResult = EEPROM_TEST_FAIL_WRITING_BUF;
    }
    else
    {
      //The last page's write cycle must end before the read is acknowledged
      CAT24C16_WAIT_WRITE_CYCLE();
      if (CAT24C16_read(CAT24C16_TEST_OFFSET, CAT24C16_TEST_SIZE, test_eeprom_rd))
      {
        testResult = EEPROM_TEST_FAIL_READING_BUF;
      }
      else if (memcmp(test_eeprom_wr, test_eeprom_rd, CAT24C16_TEST_SIZE))
      {
        testResult = EEPROM_TEST_FAIL_BUF_COMPARISON;
      }
    }
  }

  //From the first write on, the test region may hold test data whatever
  //happened after it, so every outcome puts the backup back (DEV-1495). Those
  //bytes are host offsets 0-127 of the daughter-card memory.
  if (CAT24C16_restoreTestRegion(test_eeprom_backup, test_eeprom_rd))
  {
    testResult = (testResult == EEPROM_TEST_PASS) ?
        EEPROM_TEST_FAIL_RESTORING_BACKUP :
        (uint8_t) (testResult | EEPROM_TEST_FAIL_NOT_RESTORED);
  }

  return testResult;
}
