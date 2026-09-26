#include "isotp.h"
#include <string.h>

void IsoTp_Init(IsoTpContext *ctx, IsoTpCanSendFunc send_func)
{
    ctx->state = ISOTP_IDLE;
    ctx->total_len = 0;
    ctx->received_len = 0;
    ctx->expected_sn = 0;
    ctx->can_send = send_func;
}

IsoTpResult IsoTp_OnCanFrameReceived(IsoTpContext *ctx, const uint8_t *data, uint8_t len)
{
    uint8_t frame_type = data[0] >> 4;

    switch (frame_type)
    {
        case 0: // SF
        {
            uint8_t sf_len = data[0] & 0x0F;

            if (sf_len == 0 || sf_len > 7) {
                return ISOTP_ERROR_OVERFLOW;
            }

            memcpy(ctx->buffer, &data[1], sf_len);
            ctx->received_len = sf_len;
            ctx->total_len = sf_len;
            ctx->state = ISOTP_IDLE;

            return ISOTP_COMPLETE;
        }

        case 1: // FF
        {
            uint16_t total_len = ((data[0] & 0x0F) << 8) | data[1];

            if (total_len > ISOTP_MAX_DATA_LEN) {
                return ISOTP_ERROR_OVERFLOW;
            }

            memcpy(ctx->buffer, &data[2], 6);
            ctx->total_len = total_len;
            ctx->received_len = 6;
            ctx->expected_sn = 1;
            ctx->state = ISOTP_RECEIVING;

            uint8_t fc_frame[3];
            fc_frame[0] = 3 << 4;   // tip=FC, durum=0 (devam et)
            fc_frame[1] = 0;        // Block Size = sınırsız
            fc_frame[2] = 0;        // Separation Time = 0
            ctx->can_send(fc_frame, 3);

            return ISOTP_IN_PROGRESS;
        }

        case 2: // CF
        {
            uint8_t sn = data[0] & 0x0F;

            if (sn != ctx->expected_sn) {
                ctx->state = ISOTP_IDLE;
                return ISOTP_ERROR_WRONG_SN;
            }

            uint16_t remaining = ctx->total_len - ctx->received_len;
            uint8_t chunk = (remaining >= 7) ? 7 : remaining;

            memcpy(&ctx->buffer[ctx->received_len], &data[1], chunk);
            ctx->received_len += chunk;

            ctx->expected_sn = (ctx->expected_sn + 1) & 0x0F;

            if (ctx->received_len >= ctx->total_len) {
                ctx->state = ISOTP_IDLE;
                return ISOTP_COMPLETE;
            }

            return ISOTP_IN_PROGRESS;
        }

        default:
            return ISOTP_ERROR_WRONG_SN;
    }
}
