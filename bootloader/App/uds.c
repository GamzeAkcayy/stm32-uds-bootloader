#include "uds.h"
#include "isotp.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL 0x10
#define UDS_SID_REQUEST_DOWNLOAD           0x34
#define UDS_SID_TRANSFER_DATA              0x36
#define UDS_SID_TRANSFER_EXIT              0x37
#define UDS_POSITIVE_RESPONSE_OFFSET       0x40

#define APP_FLASH_START  0x08008000U
#define APP_FLASH_END    0x0800C000U   /* son geçerli adres + 1 (Sektör 2) */

static IsoTpCanSendFunc uds_send;

typedef struct {
    uint32_t downloadAddress;
    uint32_t downloadSize;
    uint32_t bytesWritten;
    uint8_t  expectedBlock;
    uint8_t  downloadActive;
} UdsState;

static UdsState uds_state;

/* UDS cevabını ISO-TP tek parça (SF) zarfına koyup gönderir (n <= 7) */
static void uds_reply(const uint8_t *payload, uint8_t n)
{
    uint8_t frame[8];
    frame[0] = n;
    memcpy(&frame[1], payload, n);
    uds_send(frame, n + 1);
}

void UDS_Init(IsoTpCanSendFunc send_func)
{
    uds_send = send_func;
}

void UDS_HandleRequest(const uint8_t *data, uint16_t len)
{
    if (len < 1) {
        return;
    }

    switch (data[0])
    {
        case UDS_SID_DIAGNOSTIC_SESSION_CONTROL:
        {
            if (len < 2) {
                return;
            }

            if (data[1] == 0x02) {   /* programming session */
                uint8_t response[2];
                response[0] = UDS_SID_DIAGNOSTIC_SESSION_CONTROL + UDS_POSITIVE_RESPONSE_OFFSET;
                response[1] = data[1];
                uds_reply(response, 2);
            }
            break;
        }

        case UDS_SID_REQUEST_DOWNLOAD:
        {
            if (len < 11) {
                return;
            }

            uint32_t address = ((uint32_t)data[3] << 24) | ((uint32_t)data[4] << 16) |
                               ((uint32_t)data[5] << 8)  | data[6];
            uint32_t size    = ((uint32_t)data[7] << 24) | ((uint32_t)data[8] << 16) |
                               ((uint32_t)data[9] << 8)  | data[10];

            /* Önce doğrula. Reddedilen istek açık oturumu da kapatır. */
            if (address < APP_FLASH_START || address >= APP_FLASH_END || size == 0 ||
                size > APP_FLASH_END - address || address % 4 != 0) {
                uds_state.downloadActive = 0;
                return;
            }

            uds_state.downloadActive = 0;   /* silme bitene kadar oturum kapalı */

            FLASH_EraseInitTypeDef eraseInit;
            uint32_t sectorError;

            eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
            eraseInit.Sector       = FLASH_SECTOR_2;
            eraseInit.NbSectors    = 1;
            eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;

            HAL_FLASH_Unlock();
            HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);
            HAL_FLASH_Lock();

            if (status != HAL_OK) {
                uint8_t nrc[3] = { 0x7F, UDS_SID_REQUEST_DOWNLOAD, 0x72 };
                uds_reply(nrc, 3);
                break;
            }

            uds_state.downloadAddress = address;
            uds_state.downloadSize    = size;
            uds_state.bytesWritten    = 0;
            uds_state.expectedBlock   = 1;
            uds_state.downloadActive  = 1;  /* her zaman en son açılır */

            uint8_t response[6];
            response[0] = UDS_SID_REQUEST_DOWNLOAD + UDS_POSITIVE_RESPONSE_OFFSET;
            response[1] = 0x40;
            response[2] = 0x00;
            response[3] = 0x00;
            response[4] = 0x00;
            response[5] = 0x42;   /* max 0x36 mesajı: SID + sayaç + 64 byte = 66 */
            uds_reply(response, 6);
            break;
        }

        case UDS_SID_TRANSFER_DATA:
        {
            if (uds_state.downloadActive == 0) {
                return;
            }

            if (len < 3) {
                return;
            }

            uint8_t  blockSequenceCounter = data[1];
            uint16_t dataLen = len - 2;

            if (dataLen % 4 != 0 || dataLen > 64) {
                return;
            }

            if (uds_state.downloadSize - uds_state.bytesWritten < dataLen) {
                return;
            }

            if (blockSequenceCounter != uds_state.expectedBlock) {
                uint8_t nrc[3] = { 0x7F, UDS_SID_TRANSFER_DATA, 0x73 };
                uds_reply(nrc, 3);
                break;
            }

            uint32_t writeAddress = uds_state.downloadAddress + uds_state.bytesWritten;
            HAL_StatusTypeDef status = HAL_OK;

            HAL_FLASH_Unlock();
            for (uint16_t i = 0; i < dataLen; i += 4)
            {
                uint32_t word = ((uint32_t)data[2 + i + 3] << 24) |
                                ((uint32_t)data[2 + i + 2] << 16) |
                                ((uint32_t)data[2 + i + 1] << 8)  |
                                 data[2 + i];
                status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, writeAddress + i, word);
                if (status != HAL_OK) {
                    break;
                }
            }
            HAL_FLASH_Lock();

            if (status != HAL_OK) {
                uint8_t nrc[3] = { 0x7F, UDS_SID_TRANSFER_DATA, 0x72 };
                uds_reply(nrc, 3);
                break;
            }

            uds_state.bytesWritten += dataLen;
            uds_state.expectedBlock++;

            uint8_t response[2];
            response[0] = UDS_SID_TRANSFER_DATA + UDS_POSITIVE_RESPONSE_OFFSET;
            response[1] = blockSequenceCounter;
            uds_reply(response, 2);
            break;
        }

        case UDS_SID_TRANSFER_EXIT:
        {
            if (uds_state.downloadActive == 0 ||
                uds_state.bytesWritten != uds_state.downloadSize) {
                uint8_t nrc[3] = { 0x7F, UDS_SID_TRANSFER_EXIT, 0x24 };
                uds_reply(nrc, 3);
                break;
            }

            uds_state.downloadActive = 0;

            uint8_t response[1];
            response[0] = UDS_SID_TRANSFER_EXIT + UDS_POSITIVE_RESPONSE_OFFSET;
            uds_reply(response, 1);
            break;
        }

        default:
            break;
    }
}
