#if defined(BOARD_HAS_PSRAM) || defined(SIMULATOR)
#define PEANUT_GB_IS_LITTLE_ENDIAN 1
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#include <peanut_gb.h>
#endif
