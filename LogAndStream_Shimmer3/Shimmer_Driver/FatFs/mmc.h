#ifndef SHIMMER_DRIVER_FATFS_MMC_H_
#define SHIMMER_DRIVER_FATFS_MMC_H_

#include <stdint.h>

uint8_t detectCard(void);

/* What mmc_getCardId() has to give: the CID and size read at the card's last
 * initialisation, nothing since boot, or nothing because that initialisation,
 * or the read, failed. */
#define MMC_CARD_ID_NOT_READ 0
#define MMC_CARD_ID_READ     1
#define MMC_CARD_ID_FAILED   2

/* Copies the CID (16 bytes, most significant first) and the count of 512-byte
 * sectors read at the card's last initialisation, and returns MMC_CARD_ID_READ.
 * Otherwise it returns the reason there is none and copies nothing. The copy
 * outlives the card's power and its handover to the dock, so it is the card
 * that was in the unit then (DEV-1405). */
uint8_t mmc_getCardId(uint8_t *cid, uint32_t *sectors);

#endif /* SHIMMER_DRIVER_FATFS_MMC_H_ */
