#include "uds.h"
#include "isotp.h"  // cevap göndermek için lazım olacak

#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL 0x10
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40

static IsoTpCanSendFunc uds_send;

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

        default:
            break;
    }
}

