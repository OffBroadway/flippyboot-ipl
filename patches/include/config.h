#include "../../cubeboot/source/config.h"

#define USE_NATIVE_SPRINTF

#define PRINT_READDIR_NAMES

#if defined(GECKO_PRINT_ENABLE) || defined(DOLPHIN_PRINT_ENABLE)
#define DEBUG
#endif
