#ifndef HOST_SPI_READ_PROBE_H
#define HOST_SPI_READ_PROBE_H
#include <stdint.h>
struct host_spi_read_io {
 uint8_t (*read8)(unsigned);
 void (*write8)(unsigned,uint8_t);
 uint64_t (*time_us)(void);
};
struct host_spi_read_result {
 unsigned result,commands,restored;
 uint8_t id[3],status;
};
/* Requires the verified SMM access window. Only 9F and 05 may execute. */
void host_spi_read_probe(const struct host_spi_read_io *,struct host_spi_read_result *);
#endif
