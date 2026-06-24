/*
 * types.h — PC-port shim, found before decomp/include/decomp/types.h.
 *
 * The original decomp types.h uses `typedef signed long long s64` which
 * conflicts with libnx's `typedef long s64` on AArch64 (int64_t = long there).
 * This shim is found first (pc_port/include is before decomp/include/decomp
 * in the include-path order) and uses stdint.h so the types are identical
 * to whatever the toolchain defines for int64_t/uint64_t, avoiding conflicts.
 *
 * The _TYPES_H guard shadows decomp/include/decomp/types.h's own guard so
 * the original file is skipped once this one is processed.
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

#ifndef __cplusplus
#ifndef bool
typedef enum { false, true } bool;
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
