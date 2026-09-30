#include "flash_if.h"
#include "crc32.h"

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t crc;
} AppMeta;

static uint32_t sector_of(uint32_t addr)
{
    if (addr < 0x08004000U) return FLASH_SECTOR_0;
    if (addr < 0x08008000U) return FLASH_SECTOR_1;
    if (addr < 0x0800C000U) return FLASH_SECTOR_2;
    if (addr < 0x08010000U) return FLASH_SECTOR_3;
    if (addr < 0x08020000U) return FLASH_SECTOR_4;
    return FLASH_SECTOR_5 + (addr - 0x08020000U) / 0x20000U;
}

static HAL_StatusTypeDef erase_sectors(uint32_t first, uint32_t last)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t sectorError = 0;

    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.Sector       = first;
    e.NbSectors    = last - first + 1U;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &sectorError);
    HAL_FLASH_Lock();
    return st;
}

HAL_StatusTypeDef Flash_EraseAppRange(uint32_t size)
{
    uint32_t last = sector_of(FLASH_APP_START + size - 1U);
    return erase_sectors(FLASH_SECTOR_2, last);
}

HAL_StatusTypeDef Flash_EraseMeta(void)
{
    return erase_sectors(FLASH_SECTOR_11, FLASH_SECTOR_11);
}

HAL_StatusTypeDef Flash_WriteWords(uint32_t addr, const uint8_t *src, uint16_t len)
{
    HAL_StatusTypeDef st = HAL_OK;

    HAL_FLASH_Unlock();
    for (uint16_t i = 0; i < len; i += 4)
    {
        uint32_t word = ((uint32_t)src[i + 3] << 24) | ((uint32_t)src[i + 2] << 16) |
                        ((uint32_t)src[i + 1] << 8)  |  src[i];
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word);
        if (st != HAL_OK) {
            break;
        }
    }
    HAL_FLASH_Lock();
    return st;
}

HAL_StatusTypeDef Flash_WriteMeta(uint32_t size, uint32_t crc)
{
    HAL_StatusTypeDef st;

    HAL_FLASH_Unlock();
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_META_ADDR + 4U, size);
    if (st == HAL_OK) st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_META_ADDR + 8U, crc);
    if (st == HAL_OK) st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_META_ADDR, FLASH_META_MAGIC); /* en son */
    HAL_FLASH_Lock();
    return st;
}

uint8_t Flash_AppIsValid(void)
{
    const AppMeta *m = (const AppMeta *)FLASH_META_ADDR;

    if (m->magic != FLASH_META_MAGIC) return 0;
    if (m->size == 0 || m->size > FLASH_APP_END - FLASH_APP_START) return 0;
    return crc32_calc((const uint8_t *)FLASH_APP_START, m->size) == m->crc;
}

uint8_t Flash_AppLooksExecutable(void)
{
    uint32_t sp = *(volatile uint32_t *)FLASH_APP_START;
    uint32_t rv = *(volatile uint32_t *)(FLASH_APP_START + 4U);
    uint32_t pc = rv & ~1U;                    /* Thumb biti hariç adres */

    return (sp > 0x20000000U) && (sp <= 0x20020000U) &&
           ((rv & 1U) == 1U) &&
           (pc >= FLASH_APP_START) && (pc < FLASH_APP_END);
}
