#ifndef EC_MAPPING_PROBE_H
#define EC_MAPPING_PROBE_H
#include <stdint.h>
#define MAP_COUNT 16
#define MAP_PASSES 2
struct mapping_io {
    void *ctx;
    uint8_t (*status)(void *), (*data)(void *);
    void (*command)(void *, uint8_t), (*write_data)(void *, uint8_t);
    uint64_t (*us)(void *);
};
struct mapping_row {
    uint16_t address;
    uint8_t value, status[5];
    unsigned phase, valid;
    uint64_t elapsed;
};
struct mapping_report {
    unsigned result, writes, reads, incomplete, rows;
    uint8_t initial, final;
    uint64_t elapsed;
    struct mapping_row row[MAP_COUNT * MAP_PASSES];
};
/* Fixed list, only command 92. No arbitrary register read/write API. */
void ec_mapping_probe(const struct mapping_io *, struct mapping_report *);
#endif
