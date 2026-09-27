#include "uds.h"
#include "isotp.h"  // cevap göndermek için lazım olacak

#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL 0x10
#define UDS_SID_REQUEST_DOWNLOAD 0x34
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40
#define UDS_SID_TRANSFER_EXIT 0x37

static IsoTpCanSendFunc uds_send;

typedef struct {
    uint32_t downloadAddress;
    uint32_t downloadSize;
    uint8_t  downloadActive;
} UdsState;

static UdsState uds_state;

void UDS_Init(IsoTpCanSendFunc send_func)
{
    uds_send = send_func;
}

void UDS_HandleRequest(const uint8_t *data, uint16_t len)
{
    if (len < 1) {
        return;
    }

    uint8_t sid = data[0];

    switch (sid)
    {
        case UDS_SID_DIAGNOSTIC_SESSION_CONTROL:
        {
            if (len < 2) {
                return;
            }

            uint8_t session_type = data[1];

            if (session_type == 0x02)  // programming session
            {
                uint8_t response[2];
                response[0] = UDS_SID_DIAGNOSTIC_SESSION_CONTROL + UDS_POSITIVE_RESPONSE_OFFSET;
                response[1] = session_type;
                uds_send(response, 2);
            }
            break;
        }

		case UDS_SID_REQUEST_DOWNLOAD:
		{
			if (len < 11) {
				return;  // eksik istek
			}

			uint32_t address = ((uint32_t)data[3] << 24) | ((uint32_t)data[4] << 16) |
								((uint32_t)data[5] << 8)  | data[6];
			uint32_t size    = ((uint32_t)data[7] << 24) | ((uint32_t)data[8] << 16) |
								((uint32_t)data[9] << 8)  | data[10];

			uds_state.downloadAddress = address;
			uds_state.downloadSize = size;
			uds_state.downloadActive = 1;

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

		case UDS_SID_TRANSFER_EXIT:
		{
		    uint8_t response[1];
		    response[0] = UDS_SID_TRANSFER_EXIT + UDS_POSITIVE_RESPONSE_OFFSET;
		    uds_send(response, 1);
		    break;
		}

			default:
            break;
    }
}

