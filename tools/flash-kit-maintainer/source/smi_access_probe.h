#ifndef SMI_ACCESS_PROBE_H
#define SMI_ACCESS_PROBE_H
#include <stdint.h>
struct smi_access_io { uint8_t (*request)(uint8_t); uint32_t (*read)(unsigned); };
struct smi_access_result { unsigned result,requests; uint32_t before,during,after,restriction_during,restriction_after; uint8_t enable_status,disable_status; };
/* inspect returns 0=success, 1=restored failure, 2=controller unsafe: cold reset. */
void smi_access_inspect(const struct smi_access_io *,struct smi_access_result *,int (*inspect)(void));
void smi_access_probe(const struct smi_access_io *,struct smi_access_result *);
#endif
