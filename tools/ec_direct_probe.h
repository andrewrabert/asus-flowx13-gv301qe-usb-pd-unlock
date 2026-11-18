#ifndef EC_DIRECT_PROBE_H
#define EC_DIRECT_PROBE_H
#include <stdint.h>
#define DIRECT_COUNT 8
struct direct_io { void *ctx; uint8_t (*in)(void *,uint16_t); void (*out)(void *,uint16_t,uint8_t); };
struct direct_report {
 unsigned result,writes,reads,rows,restored;
 uint8_t initial,host_id[2],saved[3],values[2][DIRECT_COUNT];
};
/* No unlock, enable, RAM, MMIO data, follow or flash writes. Fixed ID-gated reads. */
void ec_direct_probe(const struct direct_io *,struct direct_report *);
#endif
