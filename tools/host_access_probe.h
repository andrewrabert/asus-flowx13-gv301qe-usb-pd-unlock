#ifndef HOST_ACCESS_PROBE_H
#define HOST_ACCESS_PROBE_H
#include <stdint.h>
struct host_access_io { uint32_t (*read)(unsigned); void (*write_control_byte)(uint8_t); };
struct host_access_result { unsigned result,writes; uint32_t before,during,after,restricted; };
void host_access_probe(const struct host_access_io *,struct host_access_result *);
#endif
