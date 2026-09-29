#include "uds.h"
#include "isotp.h"
#include "flash_if.h"
#include "crc32.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define UDS_SID_SESSION_CONTROL   0x10
#define UDS_SID_ECU_RESET         0x11
#define UDS_SID_ROUTINE_CONTROL   0x31
#define UDS_SID_REQUEST_DOWNLOAD  0x34
#define UDS_SID_TRANSFER_DATA     0x36
#define UDS_SID_TRANSFER_EXIT     0x37
#define UDS_POSITIVE_OFFSET       0x40

#define NRC_SUBFUNC_NOT_SUPPORTED 0x12
#define NRC_BAD_LENGTH            0x13
#define NRC_CONDITIONS            0x22
#define NRC_SEQUENCE_ERROR        0x24
#define NRC_OUT_OF_RANGE          0x31
#define NRC_PROGRAMMING_FAILURE   0x72
#define NRC_WRONG_BLOCK_COUNTER   0x73

static IsoTpCanSendFunc uds_send;

typedef struct {
    uint32_t downloadAddress;
    uint32_t downloadSize;
    uint32_t bytesWritten;
    uint8_t  expectedBlock;
    uint8_t  downloadActive;
    uint8_t  transferDone;
    uint8_t  programmingRequested;
} UdsState;

static UdsState uds_state;

static void uds_reply(const uint8_t *payload, uint8_t n)
{
    uint8_t frame[8];
    frame[0] = n;                       /* ISO-TP SF: tip 0, uzunluk n (n <= 7) */
    memcpy(&frame[1], payload, n);
    uds_send(frame, n + 1);
}

static void uds_nrc(uint8_t sid, uint8_t code)
{
    uint8_t p[3] = { 0x7F, sid, code };
    uds_reply(p, 3);
}

void UDS_Init(IsoTpCanSendFunc send_func)
{
    uds_send = send_func;
}

uint8_t UDS_ProgrammingRequested(void)
{
    return uds_state.programmingRequested;
}

void UDS_HandleRequest(const uint8_t *data, uint16_t len)
{
    if (len < 1) {
        return;
    }

    switch (data[0])
    {
        case UDS_SID_SESSION_CONTROL:
        {
            if (len < 2) { uds_nrc(data[0], NRC_BAD_LENGTH); break; }

            if (data[1] == 0x02) {
                uds_state.programmingRequested = 1;
                uint8_t r[2] = { UDS_SID_SESSION_CONTROL + UDS_POSITIVE_OFFSET, 0x02 };
                uds_reply(r, 2);
            } else {
                uds_nrc(data[0], NRC_SUBFUNC_NOT_SUPPORTED);
            }
            break;
        }

        case UDS_SID_ECU_RESET:
        {
            if (len < 2) { uds_nrc(data[0], NRC_BAD_LENGTH); break; }

            if (data[1] == 0x01) {
                uint8_t r[2] = { UDS_SID_ECU_RESET + UDS_POSITIVE_OFFSET, 0x01 };
                uds_reply(r, 2);
                HAL_Delay(20);           /* cevabın hatta çıkması için */
                HAL_NVIC_SystemReset();
            } else {
                uds_nrc(data[0], NRC_SUBFUNC_NOT_SUPPORTED);
            }
            break;
        }

        case UDS_SID_REQUEST_DOWNLOAD:
        {
            uds_state.downloadActive = 0;     /* her yeni istek eski oturumu kapatır */
            uds_state.transferDone   = 0;

            if (len != 11) { uds_nrc(data[0], NRC_BAD_LENGTH); break; }
            if (!uds_state.programmingRequested) { uds_nrc(data[0], NRC_CONDITIONS); break; }

            uint32_t address = ((uint32_t)data[3] << 24) | ((uint32_t)data[4] << 16) |
                               ((uint32_t)data[5] << 8)  |  data[6];
            uint32_t size    = ((uint32_t)data[7] << 24) | ((uint32_t)data[8] << 16) |
                               ((uint32_t)data[9] << 8)  |  data[10];

            if (address != FLASH_APP_START || size == 0 || size % 4 != 0 ||
                size > FLASH_APP_END - FLASH_APP_START) {
                uds_nrc(data[0], NRC_OUT_OF_RANGE);
                break;
            }

            /* Önce eski uygulamayı geçersiz kıl, sonra sil. */
            if (Flash_EraseMeta() != HAL_OK || Flash_EraseAppRange(size) != HAL_OK) {
                uds_nrc(data[0], NRC_PROGRAMMING_FAILURE);
                break;
            }

            uds_state.downloadAddress = address;
            uds_state.downloadSize    = size;
            uds_state.bytesWritten    = 0;
            uds_state.expectedBlock   = 1;
            uds_state.downloadActive  = 1;    /* her zaman en son açılır */

            uint8_t r[6] = { UDS_SID_REQUEST_DOWNLOAD + UDS_POSITIVE_OFFSET,
                             0x40, 0x00, 0x00, 0x00, 0x42 };  /* max 0x36 mesajı = 66 byte */
            uds_reply(r, 6);
            break;
        }

        case UDS_SID_TRANSFER_DATA:
        {
            if (!uds_state.downloadActive) { uds_nrc(data[0], NRC_SEQUENCE_ERROR); break; }
            if (len < 3) { uds_nrc(data[0], NRC_BAD_LENGTH); break; }

            uint16_t dataLen = len - 2;
            if (dataLen % 4 != 0 || dataLen > 64) { uds_nrc(data[0], NRC_BAD_LENGTH); break; }

            if (uds_state.downloadSize - uds_state.bytesWritten < dataLen) {
                uds_nrc(data[0], NRC_OUT_OF_RANGE);
                break;
            }

            if (data[1] != uds_state.expectedBlock) {
                uds_nrc(data[0], NRC_WRONG_BLOCK_COUNTER);
                break;
            }

            uint32_t writeAddress = uds_state.downloadAddress + uds_state.bytesWritten;
            if (Flash_WriteWords(writeAddress, &data[2], dataLen) != HAL_OK) {
                uds_nrc(data[0], NRC_PROGRAMMING_FAILURE);
                break;
            }

            uds_state.bytesWritten += dataLen;
            uds_state.expectedBlock++;

            uint8_t r[2] = { UDS_SID_TRANSFER_DATA + UDS_POSITIVE_OFFSET, data[1] };
            uds_reply(r, 2);
            break;
        }

        case UDS_SID_TRANSFER_EXIT:
        {
            if (!uds_state.downloadActive || uds_state.bytesWritten != uds_state.downloadSize) {
                uds_nrc(data[0], NRC_SEQUENCE_ERROR);
                break;
            }

            uds_state.downloadActive = 0;
            uds_state.transferDone   = 1;

            uint8_t r[1] = { UDS_SID_TRANSFER_EXIT + UDS_POSITIVE_OFFSET };
            uds_reply(r, 1);
            break;
        }

        case UDS_SID_ROUTINE_CONTROL:
        {
            /* 31 01 FF 01 <crc32 big-endian>: "yazılan imajı doğrula" */
            if (len != 8) { uds_nrc(data[0], NRC_BAD_LENGTH); break; }
            if (data[1] != 0x01 || data[2] != 0xFF || data[3] != 0x01) {
                uds_nrc(data[0], NRC_OUT_OF_RANGE);
                break;
            }
            if (!uds_state.transferDone) { uds_nrc(data[0], NRC_SEQUENCE_ERROR); break; }

            uint32_t expected = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                                ((uint32_t)data[6] << 8)  |  data[7];
            uint32_t actual = crc32_calc((const uint8_t *)uds_state.downloadAddress,
                                         uds_state.downloadSize);

            if (actual != expected) {
                uint8_t r[5] = { 0x71, 0x01, 0xFF, 0x01, 0x01 };   /* durum 1: CRC uyuşmadı */
                uds_reply(r, 5);
                break;
            }

            if (Flash_WriteMeta(uds_state.downloadSize, actual) != HAL_OK) {
                uds_nrc(data[0], NRC_PROGRAMMING_FAILURE);
                break;
            }

            uint8_t r[5] = { 0x71, 0x01, 0xFF, 0x01, 0x00 };       /* durum 0: tamam */
            uds_reply(r, 5);
            break;
        }

        default:
            break;
    }
}
