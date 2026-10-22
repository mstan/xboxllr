// SPDX-License-Identifier: GPL-3.0-or-later
// The pinned freestanding core spells these scalar IEEE operations as clang
// builtins. MSVC uses its universal C math interface under /fp:strict instead.
// No SIMD, host-native feature selection or handwritten assembly is required.
#pragma once
#include <math.h>
#define __builtin_sqrtf sqrtf
#define __builtin_sqrt sqrt
#define __builtin_fabsf fabsf
