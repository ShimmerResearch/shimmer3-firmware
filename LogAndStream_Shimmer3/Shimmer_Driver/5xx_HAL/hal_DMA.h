#ifndef HAL_DMA_H
#define HAL_DMA_H

#include <stdint.h>

//Writes an address to a 20-bit DMA address register (DMAxSA, DMAxDA).
//The device header declares those registers as __SFR_FARPTR, a function
//pointer, because that is its 20-bit type. Storing a data address in one is
//therefore an integer-to-function-pointer conversion, which cl430 reports as
//#173 "invalid type conversion" although it is exactly what is meant: the
//assignment compiles to a single MOVX.A. #173 is suppressed for this statement
//only.
#define DMA_SET_ADDRESS(reg, addr)                         \
  do                                                       \
  {                                                        \
    _Pragma("diag_push") _Pragma("diag_suppress 173")(reg) \
        = (__SFR_FARPTR) (unsigned long) (addr);           \
    _Pragma("diag_pop")                                    \
  } while (0)

extern void DMA0_init(uint16_t *srcAddr, uint16_t *destAddr, uint16_t size);
extern void DMA0_repeatTransfer(uint16_t *srcAddr, uint16_t *destAddr, uint16_t size);
extern void DMA0_enable(void);
extern void DMA0_disable(void);

extern void DMA1_init(uint8_t *srcAddr, uint8_t *destAddr, uint16_t size);
extern void DMA1_repeatTransfer(uint8_t *srcAddr, uint8_t *destAddr, uint16_t size);
extern void DMA1_enable(void);
extern void DMA1_disable(void);

extern void DMA2_init(uint16_t *srcAddr, uint16_t *destAddr, uint16_t size);
extern void DMA2_repeatTransfer(uint16_t *srcAddr, uint16_t *destAddr, uint16_t size);
extern void DMA2_enable(void);
extern void DMA2_disable(void);
extern void DMA2_transferDoneFunction(uint8_t (*transferDoneFuncPtr)(void));

//pass in a pointer to the function that will get called when
//dma0 transfer is finished
//the pass in function returns a 1 if program execution should resume
//(i.e. clear LPM3 bits)
//otherwise return 0
extern void DMA0_transferDoneFunction(uint8_t (*transferDoneFuncPtr)(void));

#endif //HAL_DMA_H
