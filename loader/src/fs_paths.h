#pragma once

/* Maps Horizon "mount:/path" paths to host paths.
 *   rom:/...    -> $NXB_ROMFS/...  (BrowserDll RomFS, read-only)
 *   <other>:/.. -> $NXB_DATA/<other>/...  (default ./nxb-data, created on demand)
 * Paths without a mount name are passed through unchanged. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns buf, or NULL if the result doesn't fit. */
const char* nxb_translate_path(const char* path, char* buf, size_t size);

#ifdef __cplusplus
}
#endif
