/* 板端 buildroot glibc 2.37 缺 __isoc23_* 符号; SDK sysroot 的 glibc 2.42 头文件
 * 在 _GNU_SOURCE(→_ISOC23_SOURCE) 下把 strtol 族重定向到 __isoc23_strtol 等
 * (__GLIBC_USE_C23_STRTOL=1), 导致板上运行期 "GLIBC_2.38 not found".
 * 本文件经 -isystem 注入, 先于 sysroot 的 features.h 命中, 关掉该重定向,
 * 让交叉产物只引用 2.37 已有的 strtol 族符号.
 * glibc 官方注释亦允许禁用: "allow that redirection to be disabled when
 * building those functions, despite _GNU_SOURCE being defined". */
#ifndef CALC_GLIBC23_SHIM_FEATURES_H
#define CALC_GLIBC23_SHIM_FEATURES_H

#include_next <features.h>

#undef __GLIBC_USE_C23_STRTOL
#define __GLIBC_USE_C23_STRTOL 0

#endif
