/*
 * hal_Stack.h
 *
 *  Created on: 16 Oct 2026
 *      Author: MarkNolan
 *
 * Stack high-water mark. Stack_paint() fills the unused part of .stack with a
 * known byte at boot. Stack_getHighWater() later finds the lowest byte that no
 * longer holds it, which is as deep as the stack has reached since.
 */

#ifndef HAL_STACK_H_
#define HAL_STACK_H_

#include <stdint.h>

/* Call once, first thing in main(), with interrupts still disabled. */
void Stack_paint(void);

/* The configured --stack_size, in bytes. */
uint16_t Stack_getSize(void);

/* The most stack used since Stack_paint(), in bytes. Equal to Stack_getSize()
 * if the stack has reached its limit, in which case it may have overflowed. */
uint16_t Stack_getHighWater(void);

#endif /* HAL_STACK_H_ */
