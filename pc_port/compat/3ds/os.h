/*
 * <3ds/os.h> for the vendored NDSP headers (third_party/libctru/include/3ds/ndsp),
 * which include it for SYSCLOCK_SOC. The os* functions themselves are in
 * libctru-lite (compat/3ds.h).
 */
#pragma once

#include <3ds/types.h>

#ifndef SYSCLOCK_SOC
#define SYSCLOCK_SOC 16756991
#endif
