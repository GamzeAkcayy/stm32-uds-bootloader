#ifndef ISOTP_H
#define ISOTP_H

#include <stdint.h>

#define ISOTP_MAX_DATA_LEN 4095

typedef enum {
    ISOTP_IDLE,
    ISOTP_RECEIVING
} IsoTpState;

typedef enum {
    ISOTP_OK,
    ISOTP_IN_PROGRESS,
    ISOTP_COMPLETE,
    ISOTP_ERROR_WRONG_SN,
    ISOTP_ERROR_OVERFLOW
} IsoTpResult;

typedef void (*IsoTpCanSendFunc)(const uint8_t *data, uint8_t len);

typedef struct {
    IsoTpState state;
    uint8_t  buffer[ISOTP_MAX_DATA_LEN];
    uint16_t total_len;
    uint16_t received_len;
    uint8_t  expected_sn;
    IsoTpCanSendFunc can_send;
} IsoTpContext;

void IsoTp_Init(IsoTpContext *ctx, IsoTpCanSendFunc send_func);
IsoTpResult IsoTp_OnCanFrameReceived(IsoTpContext *ctx, const uint8_t *data, uint8_t len);

#endif
