#include "uds.h"
#include "isotp.h"
#include "stm32f4xx_hal.h"

#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL 0x10
#define UDS_SID_REQUEST_DOWNLOAD 0x34
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40
#define UDS_SID_TRANSFER_EXIT 0x37
#define UDS_SID_TRANSFER_DATA 0x36
#define APP_FLASH_START  0x08008000U
#define APP_FLASH_END    0x0800C000U   // son geçerli adres + 1

static IsoTpCanSendFunc uds_send;

typedef struct {
	uint32_t downloadAddress;
	uint32_t downloadSize;
	uint32_t bytesWritten;
	uint8_t downloadActive;
} UdsState;

static UdsState uds_state;

void UDS_Init(IsoTpCanSendFunc send_func) {
	uds_send = send_func;
}

void UDS_HandleRequest(const uint8_t *data, uint16_t len) {
	if (len < 1) {
		return;
	}

	uint8_t sid = data[0];

	switch (sid) {
	case UDS_SID_DIAGNOSTIC_SESSION_CONTROL: {
		if (len < 2) {
			return;
		}

		uint8_t session_type = data[1];

		if (session_type == 0x02)  // programming session
				{
			uint8_t response[2];
			response[0] = UDS_SID_DIAGNOSTIC_SESSION_CONTROL
					+ UDS_POSITIVE_RESPONSE_OFFSET;
			response[1] = session_type;
			uds_send(response, 2);
		}
		break;
	}

	case UDS_SID_REQUEST_DOWNLOAD: {
		if (len < 11) {
			return;  // eksik istek
		}

		uint32_t address = ((uint32_t) data[3] << 24)
				| ((uint32_t) data[4] << 16) | ((uint32_t) data[5] << 8)
				| data[6];
		uint32_t size = ((uint32_t) data[7] << 24) | ((uint32_t) data[8] << 16)
				| ((uint32_t) data[9] << 8) | data[10];

		uds_state.downloadAddress = address;
		uds_state.downloadSize = size;
		uds_state.bytesWritten = 0;
		uds_state.downloadActive = 1;

		FLASH_EraseInitTypeDef eraseInit;
		uint32_t sectorError;

		eraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
		eraseInit.Sector = FLASH_SECTOR_2;
		eraseInit.NbSectors = 1;
		eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;

		if (address < APP_FLASH_START || address >= APP_FLASH_END || size == 0
				|| size > APP_FLASH_END - address || address % 4 != 0) {
			return;
		}
		HAL_FLASH_Unlock();
		HAL_FLASHEx_Erase(&eraseInit, &sectorError);
		HAL_FLASH_Lock();

		uint8_t response[6];
		response[0] = UDS_SID_REQUEST_DOWNLOAD + UDS_POSITIVE_RESPONSE_OFFSET;
		response[1] = 0x40;
		response[2] = 0x00;
		response[3] = 0x00;
		response[4] = 0x00;
		response[5] = 0xFF;  // örnek: max blok boyutu 255 byte

		uds_send(response, 6);
		break;
	}

	case UDS_SID_TRANSFER_DATA: {
		if (uds_state.downloadActive == 0) {
			return;
		}

		if (len < 3) {          // SID + sayaç + en az 1 byte veri
			return;
		}

		uint8_t blockSequenceCounter = data[1];
		uint16_t dataLen = len - 2;

		if (dataLen % 4 != 0 || dataLen > 64) {
			return;
		}

		if (uds_state.downloadSize - uds_state.bytesWritten < dataLen) {
			return;
		}

		uint32_t writeAddress = uds_state.downloadAddress
				+ uds_state.bytesWritten;

		HAL_StatusTypeDef status = HAL_OK;

		HAL_FLASH_Unlock();
		for (uint16_t i = 0; i < dataLen; i += 4) {
			uint32_t word = ((uint32_t) data[2 + i + 3] << 24)
					| ((uint32_t) data[2 + i + 2] << 16)
					| ((uint32_t) data[2 + i + 1] << 8) | data[2 + i];
			status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, writeAddress + i,
					word);
			if (status != HAL_OK) {
				break;
			}
		}
		HAL_FLASH_Lock();

		if (status != HAL_OK) {
			uint8_t nrc[3] = { 0x7F, UDS_SID_TRANSFER_DATA, 0x72 };
			uds_send(nrc, 3);
			break;
		}

		uds_state.bytesWritten += dataLen;

		uint8_t response[2];
		response[0] = UDS_SID_TRANSFER_DATA + UDS_POSITIVE_RESPONSE_OFFSET;
		response[1] = blockSequenceCounter;
		uds_send(response, 2);
		break;
	}

	case UDS_SID_TRANSFER_EXIT: {
		uds_state.downloadActive = 0;

		uint8_t response[1];
		response[0] = UDS_SID_TRANSFER_EXIT + UDS_POSITIVE_RESPONSE_OFFSET;
		uds_send(response, 1);
		break;
	}

	default:
		break;
	}
}
