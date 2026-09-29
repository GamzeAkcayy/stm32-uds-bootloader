#ifndef UDS_H
#define UDS_H

#include <stdint.h>
#include "isotp.h"

void    UDS_Init(IsoTpCanSendFunc send_func);
void    UDS_HandleRequest(const uint8_t *data, uint16_t len);
uint8_t UDS_ProgrammingRequested(void);

#endif
