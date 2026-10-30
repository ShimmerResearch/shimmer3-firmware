/*
 * hal_TA0.h
 *
 * Timer_A0, shared by SD sync (CCR1) and the TTL input capture (CCR4), and the
 * TTL input itself: P1.5 as TA0.CCI4A. The functions are declared in
 * log-and-stream-common's log_and_stream_externs.h, which is what calls them.
 */

#ifndef HAL_TA0_H_
#define HAL_TA0_H_

#include <stdint.h>

void TA0_start(uint8_t user);
void TA0_stop(uint8_t user);
uint8_t TA0_captureIsr(void);

void Board_evtTtlArm(uint8_t enable);
uint8_t Board_evtTtlLevel(void);

#endif /* HAL_TA0_H_ */
