#include "gs_link.h"
#include <string.h>

const char *gs_rx_line(gs_rx_t *q)
{
    while (q->r != q->w) {
        char c = (char)q->buf[q->r];
        q->r = (uint16_t)((q->r + 1) % GS_RING);
        if (c == '\n') {
            q->line[q->ll] = 0;
            q->ll = 0;
            return q->line;
        }
        if (c == '\r') continue;
        if (q->ll < GS_LINE_MAX - 1) q->line[q->ll++] = c;
        else q->ll = 0;                       /* longer than any command: garbage, start over */
    }
    return NULL;
}

int gs_tc_ok(const char *line)
{
    size_t n = strlen(line);
    if (!n || n > GS_TC_MAX - 2) return 0;
    if (line[0] == '$') return 0;
    if (strncmp(line, "SIM", 3) == 0) return 0;   /* ponytail: no bench flag; delete this line for simulator tests */
    for (const char *c = line; *c; c++)
        if (*c < 0x20 || *c > 0x7E) return 0;
    return 1;
}
