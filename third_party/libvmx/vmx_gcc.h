#pragma once
#if defined(__GNUC__) && !defined(__clang__)
#define VMX_GCC_ATTR_align(n) aligned(n)
#define __declspec(x) __attribute__((VMX_GCC_ATTR_##x))
#endif
