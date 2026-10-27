/* Host self-check for the ground station line assembler and telecommand gate.
 *   gcc -I../Core/Inc test_gs_link.c ../Core/Src/gs_link.c -o t && ./t
 * Not part of the STM32 build (only Core/ and Drivers/ are source folders). */
#include "gs_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *TLM = "$,011,125,42,0.10|-0.20|9.81,1.00|2.00|3.00,10.50|20.50|30.50,1.50|2.50|180.00,"
                         "512.30,948.12,27.40,0.30|0.40|-5.20,41400.0,18.520430,73.856743,560.0,9,0.30|0.40,"
                         "0.45,7.90,83.0,3,1A,0,1,#";

static void feed(gs_rx_t *q, const char *s) { while (*s) gs_rx_push(q, (uint8_t)*s++); }

int main(void)
{
    static gs_rx_t q;
    const char *l;

    /* a line split across two pushes: nothing until the '\n', then the whole line, CR/LF stripped */
    feed(&q, "BUZZ");
    assert(gs_rx_line(&q) == NULL);
    feed(&q, " ON\r\n");
    l = gs_rx_line(&q);
    assert(l && strcmp(l, "BUZZ ON") == 0);
    assert(gs_rx_line(&q) == NULL);

    /* several lines queued: each comes out once, in order */
    feed(&q, "STATE 2\r\n\n CAPTURE\n");
    l = gs_rx_line(&q); assert(l && strcmp(l, "STATE 2") == 0);
    l = gs_rx_line(&q); assert(l && *l == 0);                      /* empty line */
    l = gs_rx_line(&q); assert(l && strcmp(l, " CAPTURE") == 0);
    assert(gs_rx_line(&q) == NULL);

    /* over-long garbage restarts the assembler; the next real line still comes out whole */
    for (int i = 0; i < GS_LINE_MAX + 50; i++) gs_rx_push(&q, 'x');
    feed(&q, "\nIMG\n");
    l = gs_rx_line(&q); assert(l && strlen(l) < GS_LINE_MAX);
    l = gs_rx_line(&q); assert(l && strcmp(l, "IMG") == 0);

    /* ring full: bytes are dropped, nothing corrupts, reader recovers */
    for (int i = 0; i < 3 * GS_RING; i++) gs_rx_push(&q, 'y');
    while (gs_rx_line(&q)) ;
    feed(&q, "\n");
    while (gs_rx_line(&q)) ;
    feed(&q, "CALIB\n");
    l = gs_rx_line(&q); assert(l && strcmp(l, "CALIB") == 0);

    /* PC -> radio telecommand gate */
    assert(gs_tc_ok("BUZZ ON"));
    assert(gs_tc_ok("TIME 12 30 00"));
    assert(gs_tc_ok("IMGACK,7,OK"));
    assert(!gs_tc_ok(""));
    assert(!gs_tc_ok(TLM));                                /* telemetry looped back: FC drops '$' lines */
    assert(!gs_tc_ok("SIM,1,100,0,0"));
    assert(!gs_tc_ok("SIM OFF"));
    assert(!gs_tc_ok("BUZZ\x08 ON"));                      /* backspace is not printable */
    { char big[GS_TC_MAX + 8]; memset(big, 'A', sizeof big - 1); big[sizeof big - 1] = 0;
      assert(!gs_tc_ok(big)); big[GS_TC_MAX - 2] = 0; assert(gs_tc_ok(big)); }

    puts("gs_link ok");
    return 0;
}
