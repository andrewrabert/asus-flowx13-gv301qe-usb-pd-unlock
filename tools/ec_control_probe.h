#ifndef EC_CONTROL_PROBE_H
#define EC_CONTROL_PROBE_H
#include <stdint.h>
#define MAP_COUNT 16
#define MAP_PASSES 2
struct control_io {
    void *ctx;
    uint8_t (*status)(void *), (*data)(void *);
    void (*command)(void *, uint8_t), (*write_data)(void *, uint8_t);
    uint64_t (*us)(void *);
};
struct control_row {
    uint16_t address;
    uint8_t value, status[5];
    unsigned phase, valid;
    uint64_t elapsed;
};
struct control_report {
    unsigned result, writes, reads, incomplete, rows;
    uint8_t initial, final;
    uint64_t elapsed;
    struct control_row row[MAP_COUNT * MAP_PASSES];
};
/* Fixed list, only command 92. No arbitrary register read/write API. */
void ec_control_probe(const struct control_io *, struct control_report *);
#endif
