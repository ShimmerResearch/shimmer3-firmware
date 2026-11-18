#ifndef CAT24C16_H
#define CAT24C16_H

#include <stdint.h>

//7 bit address I2C address
//lower 3 bits are highest order bits of memory address
#define CAT24C16_ADDR                0x50

#define CAT24C16_PAGE_SIZE           16
#define CAT24C16_BLOCK_SIZE          256
#define CAT24C16_READ_SIZE           128
#define CAT24C16_TOTAL_SIZE          2048
#define CAT24C16_TEST_OFFSET         16
#define CAT24C16_TEST_SIZE           (8 * CAT24C16_PAGE_SIZE)

//How many times CAT24C16_test() tries to put the test region back
#define CAT24C16_TEST_RESTORE_TRIES  3

#define CAT24C16_OUT_OF_BOUNDS_ERROR 1000

enum CAT24C16_TEST
{
  EEPROM_TEST_PASS = 0,
  EEPROM_TEST_FAIL_INITIAL_BACKUP,
  EEPROM_TEST_FAIL_WRITING_BUF,
  EEPROM_TEST_FAIL_READING_BUF,
  EEPROM_TEST_FAIL_BUF_COMPARISON,
  //Every pass succeeded but the test region could not be put back
  EEPROM_TEST_FAIL_RESTORING_BACKUP,
};

//OR'd into EEPROM_TEST_FAIL_WRITING_BUF, _READING_BUF or _BUF_COMPARISON when,
//after that failure, the test region could not be put back either. Its bytes
//then hold test data, not what was there before the test.
#define EEPROM_TEST_FAIL_NOT_RESTORED 0x80

//initialise the I2C for use with the CAT24C16
//and also power on
void CAT24C16_init(void);

//power on the CAT24C16 chip
void CAT24C16_powerOn(void);

//power off the CAT24C16 chip
void CAT24C16_powerOff(void);

//Read from the CAT24C16 EEPROM
//address = starting address to read from
//length = number of bytes to read
//outBuffer = location to put read bytes
//Note the CAT24C16 had 2048 bytes of storage
//So address + length must be <= 2048
int32_t CAT24C16_read(uint16_t address, uint16_t length, uint8_t *outBuffer);

//Write to the CAT24C16 EEPROM
//address = starting address to write to
//length = number of bytes to write
//data = bytes to write
//Note the CAT24C16 had 2048 bytes of storage
//So address + length must be <= 2048
//It takes 5ms per write cycle
//Each write cycle can write up to 16bytes of data
//but only within a 16-byte page (of which there are
//128 in the CAT24C16)
//It waits out a write cycle before each page it writes, not after the last
//one, so a read straight after it must wait 5ms first
int32_t CAT24C16_write(uint16_t address, uint16_t length, uint8_t *data);

//Write/read test over CAT24C16_TEST_SIZE bytes from CAT24C16_TEST_OFFSET.
//The bytes there are backed up first and, once anything has been written,
//put back and verified whatever the outcome.
//Returns EEPROM_TEST_PASS or an enum CAT24C16_TEST failure, with
//EEPROM_TEST_FAIL_NOT_RESTORED set if the region could not be put back.
uint8_t CAT24C16_test(void);

#endif //CAT24C16_H
