/*
 * gs_link.h - ground station link logic, HAL-free so it builds on the host (Tests/).
 *
 * Per UART: a byte ring filled from the RX interrupt and drained by the main loop.
 * Radio rings are dumped raw to the PC. USB rings are drained one line at a time and
 * each line is gated as a telecommand before it goes out on the radio.
 */
#ifndef GS_LINK_H
#define GS_LINK_H

#include <stdint.h>
#include <stddef.h>

#define GS_RING      1024   /* bytes buffered per UART while the main loop is busy elsewhere (~4 lines) */
#define GS_LINE_MAX  320    /* longest line the PC side assembles */
#define GS_TC_MAX    120    /* rf_pcb-fw TC_MAX: longest telecommand incl. CRLF */

typedef struct {
    volatile uint16_t w, r;          /* w: ISR writes, r: main loop writes */
    uint8_t  buf[GS_RING];
    uint16_t ll;
    char     line[GS_LINE_MAX];
} gs_rx_t;

/* ISR side. A full ring drops the byte. */
static inline void gs_rx_push(gs_rx_t *q, uint8_t c)
{
    uint16_t n = (uint16_t)((q->w + 1) % GS_RING);
    if (n != q->r) { q->buf[q->w] = c; q->w = n; }
}

/* Main-loop side. Pulls bytes until a '\n' completes a line; returns it with CR/LF
 * stripped and NUL terminated (valid until the next call), or NULL if none is complete. */
const char *gs_rx_line(gs_rx_t *q);

/* PC -> radio gate (TELECOMMAND_HANDOFF.md s.2, s.3, s.6, s.7): non-empty, <= GS_TC_MAX-2 chars,
 * printable ASCII only, not starting with '$' (FC drops it), not a SIM line (flight safety).
 * Returns 1 if the line may be sent to the FC as a telecommand. */
int gs_tc_ok(const char *line);

#endif
