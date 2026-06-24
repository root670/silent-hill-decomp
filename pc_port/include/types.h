#ifndef __SWITCH__
#include "decomp/types.h"
#else
/* Switch: stdint-based type definitions to avoid conflict with libnx s64/u64.
 * switch_types_compat.h is force-included before each TU and defines _TYPES_H
 * first, so this file is never reached on Switch. It exists only to satisfy
 * quoted-include resolution if something does #include "types.h" directly. */
#endif
