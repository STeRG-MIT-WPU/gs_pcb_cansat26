/* Host self-check for the ground station line assembler and classifier.
 *   gcc -I../Core/Inc test_gs_link.c ../Core/Src/gs_link.c ../Core/Src/telemetry.c -o t && ./t
 * Not part of the STM32 build (only Core/ and Drivers/ are source folders). */
#include "gs_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *TLM = "$,011,125,42,0.10|-0.20|9.81,1.00|2.00|3.00,10.50|20.50|30.50,1.50|2.50|180.00,"
                         "512.30,948.12,27.40,0.30|0.40|-5.20,41400.0,18.520430,73.856743,560.0,9,0.30|0.40,"
                         "0.45,7.90,83.0,3,1A,#";

static void feed(gs_rx_t *q, const char *s) { while (*s) gs_rx_push(q, (uint8_t)*s++); }

int main(void)
{
    static gs_rx_t q;
    const char *l;

    /* LoRa delivers the frame as a chunk, a pause, then the rest: no line until the '\n' */
    char frame[GS_LINE_MAX], first[GS_LINE_MAX];
    snprintf(frame, sizeof frame, "%s\r\n", TLM);
    size_t cut = strlen(TLM) / 2;
    memcpy(first, frame, cut); first[cut] = 0;
    feed(&q, first);
    assert(gs_rx_line(&q) == NULL);
    feed(&q, frame + cut);
    l = gs_rx_line(&q);
    assert(l && strcmp(l, TLM) == 0);                 /* CR/LF stripped */
    assert(gs_classify(l) == GS_TLM);
    assert(gs_rx_line(&q) == NULL);

    /* several lines queued: each comes out once, in order */
    feed(&q, "$IMG,3,1,10,180,AAAA*5F\r\n$,garbage\n\n$IMGEND,3,10,69000*1C\n");
    l = gs_rx_line(&q); assert(l && gs_classify(l) == GS_IMG);
    l = gs_rx_line(&q); assert(l && gs_classify(l) == GS_DROP);    /* truncated telemetry */
    l = gs_rx_line(&q); assert(l && *l == 0);                      /* empty line */
    l = gs_rx_line(&q); assert(l && gs_classify(l) == GS_IMG && strncmp(l, "$IMGEND", 7) == 0);
    assert(gs_rx_line(&q) == NULL);

    /* a telecommand echo or noise is dropped */
    assert(gs_classify("CMD,011,CX,ON") == GS_DROP);
    assert(gs_classify("") == GS_DROP);

    /* over-long garbage restarts the assembler; the next real line still comes out whole */
    for (int i = 0; i < GS_LINE_MAX + 50; i++) gs_rx_push(&q, 'x');
    feed(&q, "\n$IMGSTART,4,69000,384*2A\n");
    l = gs_rx_line(&q); assert(l && strlen(l) < GS_LINE_MAX && gs_classify(l) == GS_DROP);
    l = gs_rx_line(&q); assert(l && gs_classify(l) == GS_IMG);

    /* ring full: bytes are dropped, nothing corrupts, reader recovers */
    for (int i = 0; i < 3 * GS_RING; i++) gs_rx_push(&q, 'y');
    while (gs_rx_line(&q)) ;
    feed(&q, "\n");
    while (gs_rx_line(&q)) ;
    feed(&q, "$IMG,1,1,1,4,AAAA*00\n");
    l = gs_rx_line(&q); assert(l && gs_classify(l) == GS_IMG);

    puts("gs_link ok");
    return 0;
}
