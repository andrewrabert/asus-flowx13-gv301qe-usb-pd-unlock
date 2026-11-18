#ifndef EC_INDIRECT_PROBE_H
#define EC_INDIRECT_PROBE_H
#include "ec_mapping_probe.h"
#define INDIRECT_BYTES 96
struct indirect_report {
    unsigned result, writes, reads, incomplete, dirty, restored, samples;
    unsigned phase, failed_phase, failed_address, current_address;
    uint8_t status, saved[4], restore_check[4], sample[2][INDIRECT_BYTES];
    uint64_t elapsed;
};
void ec_indirect_probe(const struct mapping_io *,struct indirect_report *);
/* Candidate 192 KiB window, not an assertion of physical flash geometry. */
#define EC_BACKUP_WINDOW 0x30000u
void ec_indirect_page(const struct mapping_io *,unsigned offset,uint8_t page[256],struct indirect_report *);
void ec_chip_info(const struct mapping_io *,uint8_t values[6],struct indirect_report *);
#endif
