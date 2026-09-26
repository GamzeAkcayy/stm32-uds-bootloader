#include "isotp.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

// Sahte (mock) CAN gönderme fonksiyonu: gerçekten CAN'e göndermez,
// gönderilen veriyi burada saklar ki test içinde kontrol edebilelim.
static uint8_t last_sent[8];
static uint8_t last_sent_len = 0;

void MockCanSend(const uint8_t *data, uint8_t len)
{
    memcpy(last_sent, data, len);
    last_sent_len = len;
    printf("CAN gonderildi: ");
    for (int i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");
}

int main(void)
{
    IsoTpContext ctx;
    IsoTp_Init(&ctx, MockCanSend);

    // TEST 1: Single Frame
    printf("--- TEST 1: SF ---\n");
    uint8_t sf_msg[8] = {0x03, 0xAA, 0xBB, 0xCC, 0, 0, 0, 0};
    IsoTpResult r = IsoTp_OnCanFrameReceived(&ctx, sf_msg, 8);

    assert(r == ISOTP_COMPLETE);
    assert(ctx.total_len == 3);
    assert(ctx.buffer[0] == 0xAA);
    assert(ctx.buffer[1] == 0xBB);
    assert(ctx.buffer[2] == 0xCC);
    printf("TEST 1 GECTI\n\n");

    // TEST 2: First Frame + Consecutive Frame
    printf("--- TEST 2: FF+CF ---\n");
    IsoTp_Init(&ctx, MockCanSend);  // context'i sıfırla

    uint8_t ff_msg[8] = {0x10, 0x0F, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    // data[0]=0x10 (tip=FF, uzunluk üst biti=0), data[1]=0x0F (15, uzunluk alt biti)
    // data[2..7] = ilk 6 byte: 01 02 03 04 05 06

    r = IsoTp_OnCanFrameReceived(&ctx, ff_msg, 8);
    assert(r == ISOTP_IN_PROGRESS);
    assert(ctx.total_len == 15);
    assert(ctx.received_len == 6);
    assert(last_sent_len == 3);        // FC gonderildi mi kontrol
    assert(last_sent[0] == 0x30);      // FC byte'i dogru mu (3<<4)
    printf("FF adimi gecti (FC gonderildi)\n");

    uint8_t cf0_msg[8] = {0x21, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D};
    // data[0]=0x21 (tip=CF, sira=1), data[1..7]=7 byte veri

    r = IsoTp_OnCanFrameReceived(&ctx, cf0_msg, 8);
    assert(r == ISOTP_IN_PROGRESS);
    assert(ctx.received_len == 13);
    printf("CF #0 adimi gecti\n");

    uint8_t cf1_msg[8] = {0x22, 0x0E, 0x0F, 0, 0, 0, 0, 0};
    // data[0]=0x22 (tip=CF, sira=2), data[1..2]=son 2 byte

    r = IsoTp_OnCanFrameReceived(&ctx, cf1_msg, 8);
    assert(r == ISOTP_COMPLETE);
    assert(ctx.received_len == 15);
    assert(ctx.buffer[14] == 0x0F);   // son byte dogru mu
    printf("CF #1 adimi gecti, aktarim tamamlandi\n");
    printf("TEST 2 GECTI\n\n");

    printf("Tum testler gecti.\n");
    return 0;
}
