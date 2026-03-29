#ifndef DOLPHIN_TYPES_H
#define DOLPHIN_TYPES_H

#if _WIN64 || __LP64__
#define BIT_64 1
#else
#define BIT_64 0
#endif

#ifdef TARGET_PC
#include <stdint.h>
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
typedef signed char s8;
typedef signed short int s16;
typedef signed long s32;
typedef signed long long int s64;
typedef unsigned char u8;
typedef unsigned short int u16;
typedef unsigned long u32;
typedef unsigned long long int u64;
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

typedef char *Ptr;

#if defined(TARGET_PC)
#include <stdbool.h>
typedef int BOOL;
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#endif

#ifdef TARGET_PC
#include <stddef.h>
#else
#ifndef NULL
#define NULL 0
#endif
#endif
#ifndef __cplusplus
#ifndef nullptr
#define nullptr NULL
#endif
#endif

#if defined(__MWERKS__)
#define AT_ADDRESS(addr) : (addr)
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#elif defined(__GNUC__)
#define AT_ADDRESS(addr)
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#elif defined(_MSC_VER)
#define AT_ADDRESS(addr)
#define ATTRIBUTE_ALIGN(num)
#else
#error unknown compiler
#endif

#ifndef DECL_WEAK
#if defined(__MWERKS__)
#define DECL_WEAK __declspec(weak)
#elif defined(__GNUC__)
#define DECL_WEAK __attribute__((weak))
#elif defined(_MSC_VER)
#define DECL_WEAK
#else
#error unknown compiler
#endif
#endif

#if TARGET_PC && __cplusplus
#define NORETURN [[noreturn]]
#else
#define NORETURN
#endif

#ifdef __MWERKS__
#define __REGISTER register
#else
#define __REGISTER
#endif

#ifdef _MSC_VER
#include <sal.h>
#define FORMAT_STRING_PARAM _Printf_format_string_
#else
#define FORMAT_STRING_PARAM
#endif

#if defined(__GNUC__)
#define FORMAT_STRING_FUNC(type, string_index, first_to_check) __attribute__((format(type, string_index, first_to_check)))
#else
#define FORMAT_STRING_FUNC(type, string_index, first_to_check)
#endif

#endif
