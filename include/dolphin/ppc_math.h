#ifndef DOLPHIN_PPC_MATH_H
#define DOLPHIN_PPC_MATH_H

#include <math.h>
#include <stdint.h>

// frsqrte matching courtesy of Geotale, with reference to https://achurch.org/cpu-tests/ppc750cl.s

struct BaseAndDec32 {
    uint32_t base;
    int32_t dec;
};

struct BaseAndDec64 {
    uint64_t base;
    int64_t dec;
};

union c32 {
    uint32_t u;
    float f;
};

union c64 {
    uint64_t u;
    double f;
};

#define EXPONENT_SHIFT_F64 ((uint64_t)52)
#define MANTISSA_MASK_F64  ((uint64_t)0x000fffffffffffffULL)
#define EXPONENT_MASK_F64  ((uint64_t)0x7ff0000000000000ULL)
#define SIGN_MASK_F64      ((uint64_t)0x8000000000000000ULL)

static const struct BaseAndDec64 RSQRTE_TABLE[32] = {
    {0x69fa000000000ULL, -0x15a0000000LL},
    {0x5f2e000000000ULL, -0x13cc000000LL},
    {0x554a000000000ULL, -0x1234000000LL},
    {0x4c30000000000ULL, -0x10d4000000LL},
    {0x43c8000000000ULL, -0x0f9c000000LL},
    {0x3bfc000000000ULL, -0x0e88000000LL},
    {0x34b8000000000ULL, -0x0d94000000LL},
    {0x2df0000000000ULL, -0x0cb8000000LL},
    {0x2794000000000ULL, -0x0bf0000000LL},
    {0x219c000000000ULL, -0x0b40000000LL},
    {0x1bfc000000000ULL, -0x0aa0000000LL},
    {0x16ae000000000ULL, -0x0a0c000000LL},
    {0x11a8000000000ULL, -0x0984000000LL},
    {0x0ce6000000000ULL, -0x090c000000LL},
    {0x0862000000000ULL, -0x0898000000LL},
    {0x0416000000000ULL, -0x082c000000LL},
    {0xffe8000000000ULL, -0x1e90000000LL},
    {0xf0a4000000000ULL, -0x1c00000000LL},
    {0xe2a8000000000ULL, -0x19c0000000LL},
    {0xd5c8000000000ULL, -0x17c8000000LL},
    {0xc9e4000000000ULL, -0x1610000000LL},
    {0xbedc000000000ULL, -0x1490000000LL},
    {0xb498000000000ULL, -0x1330000000LL},
    {0xab00000000000ULL, -0x11f8000000LL},
    {0xa204000000000ULL, -0x10e8000000LL},
    {0x9994000000000ULL, -0x0fe8000000LL},
    {0x91a0000000000ULL, -0x0f08000000LL},
    {0x8a1c000000000ULL, -0x0e38000000LL},
    {0x8304000000000ULL, -0x0d78000000LL},
    {0x7c48000000000ULL, -0x0cc8000000LL},
    {0x75e4000000000ULL, -0x0c28000000LL},
    {0x6fd0000000000ULL, -0x0b98000000LL},
};

#ifdef _MSC_VER
#include <intrin.h>
static inline uint32_t ppc_clz64(uint64_t x) {
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return 63u - (uint32_t)idx;
}
#else
static inline uint32_t ppc_clz64(uint64_t x) {
    return (uint32_t)__builtin_clzll(x);
}
#endif

static inline double frsqrte(double val) {
    union c64 bits;
    uint64_t mantissa;
    int64_t exponent;
    int sign;
    uint32_t key;
    uint64_t new_exp;
    const struct BaseAndDec64 *entry;
    union c64 result;

    bits.f = val;
    mantissa = bits.u & MANTISSA_MASK_F64;
    exponent = (int64_t)(bits.u & EXPONENT_MASK_F64);
    sign = (bits.u & SIGN_MASK_F64) != 0;

    if (mantissa == 0 && exponent == 0) {
        return copysign(INFINITY, bits.f);
    }

    if ((uint64_t)exponent == EXPONENT_MASK_F64) {
        if (mantissa == 0) {
            return sign ? NAN : 0.0;
        }
        return val;
    }

    if (sign) {
        return NAN;
    }

    if (exponent == 0) {
        uint32_t shift = ppc_clz64(mantissa) - (uint32_t)(63 - EXPONENT_SHIFT_F64);
        mantissa <<= shift;
        mantissa &= MANTISSA_MASK_F64;
        exponent -= (int64_t)(shift - 1) << EXPONENT_SHIFT_F64;
    }

    key = (uint32_t)(((uint64_t)exponent | mantissa) >> 37);
    new_exp = ((uint64_t)((0xbfcLL << EXPONENT_SHIFT_F64) - exponent) >> 1) & EXPONENT_MASK_F64;

    entry = &RSQRTE_TABLE[0x1fu & (key >> 11)];
    result.u = new_exp | (uint64_t)(entry->base + entry->dec * (int64_t)(key & 0x7ffu));
    return result.f;
}

// One Newton-Raphson step
static inline float ppc_rsqrte(float x) {
    double rsqrt_d = frsqrte((double)x);
    float nwork0 = (float)(rsqrt_d * rsqrt_d);
    float nwork1 = (float)(rsqrt_d * 0.5);
    nwork0 = fmaf(-nwork0, x, 3.0f);
    return nwork0 * nwork1;
}

#endif // DOLPHIN_PPC_MATH_H
