#ifndef HOST_SPI_WRITER_H
#define HOST_SPI_WRITER_H
#include "host_spi_read_probe.h"
/* EXPERIMENTAL. Stock-sector erase/restore demonstrated on hardware.
 * Modified firmware execution remains unverified.
 * Caller must establish/verify SMM unlock, SMI+interrupt exclusion, stable TSC,
 * exact platform identity and power. These prerequisites are not implemented here.
 * dry_run=1 performs complete preflight with NO WREN or writing commands;
 * modified may be NULL in this mode. HOST_SPI_PREFLIGHT_ONLY also refuses all
 * writing opcodes in the transport and refuses dry_run=0 before MMIO access.
 * Inputs are two 4KiB sectors in order 3D000,42000 from the guarded bundle.
 */
struct host_spi_writer_result {
 unsigned result; /* 0 success,1 refused,2 failed but idle/restored,3 UNSAFE.
                   * A nonzero dirty_mask prohibits ordinary boot even if idle. */
 unsigned commands,restored,dirty_mask,erases,programs;
 uint8_t failed_opcode;
 uint32_t failed_address;
};
void host_spi_writer(const struct host_spi_read_io *,const uint8_t original[8192],
                     const uint8_t modified[8192],int dry_run,
                     struct host_spi_writer_result *);
/* Erase and restore original contents of sector 3D000 only; verifies BOTH
 * sectors before and after. Destructive test: interrupted restoration can brick
 * the laptop. No modified image accepted. HOST_SPI_STOCK_RESTORE_ONLY additionally
 * refuses generic write mode and mutation commands outside 3D000..3DFFF.
 * HOST_SPI_PREFLIGHT_ONLY refuses this entry before MMIO.
 */
void host_spi_stock_restore(const struct host_spi_read_io *,const uint8_t original[8192],
                            struct host_spi_writer_result *);
/* Issue one 32-byte page-program of the EXISTING bytes at 3D5E0; no erase.
 * Unchanged readback does not prove the array accepted programming. This tests
 * command transport/WEL/completion. Both full sectors compared before/after.
 * HOST_SPI_STOCK_PROGRAM_ONLY refuses erase, other writes and the restore API.
 */
void host_spi_stock_program(const struct host_spi_read_io *,const uint8_t original[8192],
                            struct host_spi_writer_result *);
#endif
