/*
 * switch_types_compat.h — Force-included on Switch builds before each TU.
 *
 * libnx (switch/types.h) defines s64 = int64_t = long on AArch64 LP64.
 * The decomp types.h defines s64 = signed long long.
 * These are the same width but different base types; GCC treats it as a
 * typedef conflict error.
 *
 * By force-including this header first (-include flag in CMakeLists), we set
 * the _TYPES_H guard and supply stdint-based typedefs, so when decomp's
 * types.h is later included (e.g. via common.h in its own directory), its
 * own guard causes it to be skipped.
 *
 * bool: defined as a typedef enum (not via stdbool.h) so that `bool` remains
 * usable as a parameter name in game code (bodyprog.h:1796 has `bool bool`).
 * Files that include switch.h get stdbool.h which redefines bool as _Bool;
 * those files do not use bool as a parameter name.
 */
#ifndef _TYPES_H
#define _TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef int8_t         byte;
typedef int8_t         s8;
typedef int16_t        s16;
typedef int32_t        s32;
typedef int64_t        s64;
typedef uint8_t        u8;
typedef uint16_t       u16;
typedef uint32_t       u32;
typedef uint64_t       u64;

typedef int8_t         q0_7;
typedef int16_t        q11_4;
typedef int16_t        q7_8;
typedef int16_t        q3_12;
typedef int32_t        q27_4;
typedef int32_t        q25_6;
typedef int32_t        q23_8;
typedef int32_t        q21_10;
typedef int32_t        q19_12;
typedef int64_t        q51_12;
typedef uint8_t        q0_8;
typedef uint16_t       q8_8;
typedef uint16_t       q4_12;
typedef uint32_t       q24_8;
typedef uint32_t       q20_12;
typedef uint64_t       q52_12;

/* Define bool as a typedef so it is usable as both a type and a parameter
 * name (bodyprog.h uses `bool bool` as a function parameter).  Do not use
 * stdbool.h here: that would define `#define bool _Bool` which turns bool
 * into a keyword and makes it illegal as a parameter name. */
#ifndef __cplusplus
#ifndef bool
typedef enum { false = 0, true = 1 } bool;
#endif
#endif

#ifndef NULL
#define NULL 0
#endif

#define NO_VALUE -1

typedef struct
{
    int32_t vx;
    int32_t vy;
    int32_t vz;
} VECTOR3;

typedef struct
{
    int16_t vx;
    int16_t vy;
    int16_t vz;
} SVECTOR3;

typedef struct
{
    int16_t vx;
    int16_t vz;
} DVECTOR_XZ;

typedef struct _Pose
{
    VECTOR3  position;
    SVECTOR3 rotation;
} s_Pose;

typedef struct _Normal
{
    s8  nx;
    s8  ny;
    s8  nz;
    u8  count;
} s_Normal;

typedef union _Filename
{
    char str[8];
    u32  u32[2];
} u_Filename;

#define COMPARE_FILENAMES(a, b)                                  \
    (((u_Filename*)(a))->u32[0] != ((u_Filename*)(b))->u32[0] || \
     ((u_Filename*)(a))->u32[1] != ((u_Filename*)(b))->u32[1])

#endif /* _TYPES_H */
