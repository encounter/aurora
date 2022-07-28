#ifndef DOLPHIN_TYPES_H
#define DOLPHIN_TYPES_H

#ifdef TARGET_PC
#include <bits/wordsize.h>
#endif

#ifdef __MWERKS__
typedef signed char s8;
typedef signed short int s16;
typedef signed long s32;
typedef signed long long int s64;
typedef unsigned char u8;
typedef unsigned short int u16;
typedef unsigned long u32;
typedef unsigned long long int u64;
#else
typedef signed char s8;
typedef signed short int s16;
typedef signed int s32;
#if __WORDSIZE == 64
typedef signed long int s64;
#else
typedef signed long long int s64;
#endif
typedef unsigned char u8;
typedef unsigned short int u16;
typedef unsigned int u32;
#if __WORDSIZE == 64
typedef unsigned long int u64;
#else
typedef unsigned long long int u64;
#endif
#endif

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;

typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef float f32;
typedef double f64;

typedef volatile f32 vf32;
typedef volatile f64 vf64;

#ifdef TARGET_PC
#include <stdbool.h>
typedef bool BOOL;
#define FALSE false
#define TRUE true
#else
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#endif

#ifdef TARGET_PC
#include <stddef.h>
#else
#define NULL 0
#endif
#ifndef __cplusplus
#define nullptr NULL
#endif

#if defined(__MWERKS__)
#define AT_ADDRESS(addr) : (addr)
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#elif defined(__GNUC__)
#define AT_ADDRESS(addr) // was removed in GCC. define in linker script instead.
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#elif defined(_MSC_VER)
#define AT_ADDRESS(addr)
#define ATTRIBUTE_ALIGN(num)
#else
#error unknown compiler
#endif

#endif
