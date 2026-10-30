/*
 * hal_TA0.c
 *
 *  Created on: 30 Oct 2026
 *      Author: MarkNolan
 */

#include "hal_TA0.h"

#include <stdint.h>

#include "log_and_stream_includes.h"

/* TA0_USER_* bits, one per user. Changed with interrupts off: SD sync starts
 * and stops from both the main loop and its own interrupt. */
static uint8_t ta0Users;
/* The TTL capture takes both edges, which alternate: the next one goes this
 * way. Interrupt context, apart from arming. */
static uint8_t ttlNextLevel;

void TA0_start(uint8_t user)
{
  uint16_t gie = __get_interrupt_state();
  __disable_interrupt();
  if (ta0Users == 0U)
  {
    TA0CTL = TASSEL_1 + MC_2 + TACLR; //ACLK, continuous mode, clear TAR
  }
  ta0Users |= user;
  __set_interrupt_state(gie);
}

void TA0_stop(uint8_t user)
{
  uint16_t gie = __get_interrupt_state();
  __disable_interrupt();
  ta0Users &= (uint8_t) ~user;
  if (ta0Users == 0U)
  {
    TA0CTL = MC_0;
  }
  __set_interrupt_state(gie);
}

/* TA0R runs from ACLK, not the CPU clock, so a single read can catch it
 * changing: read until two agree. Called with interrupts off. */
static uint16_t readTa0(void)
{
  uint16_t t0, t1 = TA0R;
  do
  {
    t0 = t1;
    t1 = TA0R;
  } while (t0 != t1);
  return t1;
}

void Board_evtTtlArm(uint8_t enable)
{
  if (enable)
  {
    /* P1.5 as TA0.CCI4A, an input pulled down (P1REN with P1OUT low) so an
     * unconnected input stays low. The pull applies with P1SEL set: it
     * follows P1REN and the pin direction only (F5437A datasheet, port P1
     * diagram). */
    P1DIR &= ~BIT5;
    P1OUT &= ~BIT5;
    P1REN |= BIT5;
    P1SEL |= BIT5;

    TA0_start(TA0_USER_TTL);
    {
      /* The direction of each edge is inferred from the one before, so the
       * first must be right or every later one is inverted. Capture is
       * enabled before the level is read; an edge in between has already
       * been captured, and goes to the level just read. */
      uint16_t gie = __get_interrupt_state();
      uint8_t level;
      __disable_interrupt();
      //both edges, CCI4A, synchronised to ACLK, capture, interrupt
      TA0CCTL4 = CM_3 + CCIS_0 + SCS + CAP + CCIE;
      level = (P1IN & BIT5) ? 1U : 0U;
      ttlNextLevel = (TA0CCTL4 & CCIFG) ? level : (uint8_t) !level;
      __set_interrupt_state(gie);
    }
  }
  else
  {
    TA0CCTL4 = 0;
    TA0_stop(TA0_USER_TTL);
    //back to the plain input Board_init() leaves it as
    P1SEL &= ~BIT5;
    P1REN &= ~BIT5;
  }
}

uint8_t Board_evtTtlLevel(void)
{
  return (P1IN & BIT5) ? 1U : 0U;
}

uint8_t TA0_captureIsr(void)
{
  uint16_t cctl = TA0CCTL4;
  uint16_t ccr = TA0CCR4;
  uint16_t tar = readTa0();
  uint64_t now = RTC_get64();
  uint8_t flags = EVT_FLAG_HW_CAPTURE;
  uint8_t level;

  if (cctl & COV)
  {
    /* Captured again before this was read: at least one edge is lost. The
     * last edge taken is the one that left the input where it is now. */
    TA0CCTL4 &= ~COV;
    flags |= EVT_RAW_EDGES_LOST;
    level = (cctl & CCI) ? 1U : 0U;
  }
  else
  {
    level = ttlNextLevel;
  }
  ttlNextLevel = level ? 0U : 1U;

  /* TA0 and the RTC both count ACLK, so the capture's age in TA0 ticks is its
   * age in RTC ticks, exactly */
  return ShimEvt_isrPushTtlEdge(level, now - (uint16_t) (tar - ccr), flags);
}
