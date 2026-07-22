#ifndef SMW_COOP_PATCH_H
#define SMW_COOP_PATCH_H

#include <stddef.h>

/* Apply the bundled co-op IPS to a verified stock SMW (USA) ROM.
 * Headered and unheadered stock dumps are accepted. The output is always the
 * canonical unheadered 1 MiB co-op image. Existing valid output is reused. */
int CoopPreparePatchedRom(const char *stock_path, const char *ips_path,
                          const char *output_path,
                          char *error, size_t error_size);

#endif
