/*
 * hal_Stack.c
 *
 *  Created on: 16 Oct 2026
 *      Author: MarkNolan
 */

#include "hal_Stack.h"

#include <stdint.h>

#include <intrinsics.h>

#define STACK_PAINT_BYTE  0xA5U

/* Bytes left unpainted just below the stack pointer at the time of painting. */
#define STACK_PAINT_GUARD 8U

/* Defined by the linker. __STACK_END is the address one past the top of
 * .stack; __STACK_SIZE is an absolute symbol whose address is --stack_size. */
extern uint8_t __STACK_END;
extern uint8_t __STACK_SIZE;

static uint8_t *Stack_bottom(void)
{
  return &__STACK_END - Stack_getSize();
}

void Stack_paint(void)
{
  uint8_t *p = Stack_bottom();
  uint8_t *end = (uint8_t *) __get_SP_register() - STACK_PAINT_GUARD;

  /* Everything below the stack pointer is free. With interrupts disabled
   * nothing else can push there while this runs. */
  while (p < end)
  {
    *p++ = STACK_PAINT_BYTE;
  }
}

uint16_t Stack_getSize(void)
{
  /* Through a variable: casting the address straight to 16 bits draws a
   * truncation warning, as pointers are wider than that here. */
  uintptr_t size = (uintptr_t) &__STACK_SIZE;
  return (uint16_t) size;
}

uint16_t Stack_getHighWater(void)
{
  const uint8_t *p = Stack_bottom();
  uint16_t interruptState;

  /* Interrupts are masked for the scan. An interrupt arriving mid-scan could
   * otherwise overwrite paint below p after the loop had passed it, and the
   * result would miss that deeper use. The loop visits only the bytes still
   * painted, about 1 ms at 24 MHz with today's stack, which suits its one
   * caller, the factory test. */
  interruptState = __get_interrupt_state();
  __disable_interrupt();

  while (p < &__STACK_END && *p == STACK_PAINT_BYTE)
  {
    p++;
  }

  __set_interrupt_state(interruptState);
  return (uint16_t) (&__STACK_END - p);
}
