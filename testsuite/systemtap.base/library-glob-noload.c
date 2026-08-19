#include "sys/sdt.h"

/* Shared library with an SDT mark, not linked into the test process.
   Used by library-glob-noload.exp to glob-expand process().library()
   via resolve_library_by_path rather than DT_NEEDED. */

void
noload_ping (void)
{
  STAP_PROBE (noload, ping);
}
