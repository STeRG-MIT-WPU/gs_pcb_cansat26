/*
 * gs_link.h - ground station link logic, HAL-free so it builds on the host (Tests/).
 *
 * Per UART: a byte ring filled from the RX interrupt and drained by the main loop
 * into one line at a time. Lines from a radio are classified before they are
 * forwarded to the PC (same frame formats as rf_pcb-fw: FC telemetry + $IMG lines).
 */
#ifndef GS_LINK_H
#define GS_LINK_H

#include <stdint.h>
#include <stddef.h>

#define GS_RING      1024   /* bytes buffered per UART while the main loop is busy elsewhere (~4 lines) */
#define GS_LINE_MAX  320    /* = TLM_LINE_MAX; $IMG lines are ~215 chars */
#define GS_TC_MAX    120    /* rf_pcb-fw TC_MAX: longest telecommand incl. CRLF */

typedef struct {
    volatile uint16_t w, r;          /* w: ISR writes, r: main loop writes */
    uint8_t  buf[GS_RING];
    uint16_t ll;
    char     line[GS_LINE_MAX];
} gs_rx_t;

/* ISR side. A full ring drops the byte (the line it belongs to then fails parsing). */
static inline void gs_rx_push(gs_rx_t *q, uint8_t c)
{
    uint16_t n = (uint16_t)((q->w + 1) % GS_RING);
    if (n != q->r) { q->buf[q->w] = c; q->w = n; }
}

/* Main-loop side. Pulls bytes until a '\n' completes a line; returns it with CR/LF
 * stripped and NUL terminated (valid until the next call), or NULL if none is complete. */
const char *gs_rx_line(gs_rx_t *q);

enum { GS_DROP = 0, GS_TLM, GS_IMG };
/* GS_TLM: parses as an FC telemetry frame. GS_IMG: $IMGSTART/$IMG/$IMGEND line (relayed
 * verbatim, the PC checks the *hh checksum). GS_DROP: partial/corrupt/anything else. */
int gs_classify(const char *line);

/* PC -> radio gate (TELECOMMAND_HANDOFF.md s.2, s.3, s.6, s.7): non-empty, <= GS_TC_MAX-2 chars,
 * printable ASCII only, not starting with '$' (FC drops it), not a SIM line (flight safety).
 * Returns 1 if the line may be sent to the FC as a telecommand. */
int gs_tc_ok(const char *line);

#endif
