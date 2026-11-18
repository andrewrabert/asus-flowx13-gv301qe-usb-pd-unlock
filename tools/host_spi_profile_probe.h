#ifndef HOST_SPI_PROFILE_PROBE_H
#define HOST_SPI_PROFILE_PROBE_H
#include <stdint.h>
#include "host_spi_read_probe.h"
struct host_spi_profile_result {
 unsigned result,commands,restored;
 uint8_t id[3],status,status2,status3,sfdp[2][256],samples[3][32];
};
/* Requires the verified SMM access window. Only fixed ID/status/SFDP/array-read commands may execute. */
void host_spi_profile_probe(const struct host_spi_read_io *,struct host_spi_profile_result *);
#endif
