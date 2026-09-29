#ifndef FLASH_IF_H
#define FLASH_IF_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#define FLASH_APP_START  0x08008000U   /* Sektör 2 */
#define FLASH_APP_END    0x080E0000U   /* uygulama alanı [START, END), Sektör 2..10 */
#define FLASH_META_ADDR  0x080E0000U   /* Sektör 11: geçerlilik kaydı */
#define FLASH_META_MAGIC 0xB007AB1EU

HAL_StatusTypeDef Flash_EraseAppRange(uint32_t size);
HAL_StatusTypeDef Flash_EraseMeta(void);
HAL_StatusTypeDef Flash_WriteWords(uint32_t addr, const uint8_t *src, uint16_t len);
HAL_StatusTypeDef Flash_WriteMeta(uint32_t size, uint32_t crc);
uint8_t Flash_AppIsValid(void);          /* kayıt var ve CRC tutuyor mu */
uint8_t Flash_AppLooksExecutable(void);  /* stack ve reset vektörü makul mü */

#endif
