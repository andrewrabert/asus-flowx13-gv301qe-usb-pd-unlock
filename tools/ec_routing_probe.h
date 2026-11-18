#ifndef EC_ROUTING_PROBE_H
#define EC_ROUTING_PROBE_H
#include <stdint.h>
#define MAP_COUNT 16
#define MAP_PASSES 2
struct routing_io {
    void *ctx;
    uint8_t (*status)(void *), (*data)(void *);
    void (*command)(void *, uint8_t), (*write_data)(void *, uint8_t);
    uint64_t (*us)(void *);
};
struct routing_row {
    uint16_t address;
    uint8_t value, status[5];
    unsigned phase, valid;
    uint64_t elapsed;
};
struct routing_report {
    unsigned result, writes, reads, incomplete, rows;
    uint8_t initial, final;
    uint64_t elapsed;
    struct routing_row row[MAP_COUNT * MAP_PASSES];
};
/* Fixed list, only command 92. No arbitrary register read/write API. */
void ec_routing_probe(const struct routing_io *, struct routing_report *);
#endif
