# GCC 14 promotes -Wincompatible-pointer-types to error by default.
# bootgen's cdo-load.c has a char* = uint32_t* assignment that trips this.
CFLAGS:append = " -Wno-incompatible-pointer-types"
