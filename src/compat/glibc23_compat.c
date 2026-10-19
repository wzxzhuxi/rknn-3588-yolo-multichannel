/* 板端 buildroot glibc 2.37 无 __isoc23_* 符号, 但预编译动态库
 * (toolchain libstdc++.so.6.0.33) 引用了 __isoc23_strtoul@GLIBC_2.38 等.
 * 版本化引用不能被未版本化定义满足, 故用 .symver 给本文件的转发实现打上
 * GLIBC_2.38 版本 — 可执行文件的动态符号表中出现 __isoc23_strtoul@GLIBC_2.38
 * 定义, 加载器即可把 libstdc++ 的引用绑定到本实现 (内部转发到 2.17 时代的
 * strtol 族). 注意: 必须以 -std=gnu11 且无 _GNU_SOURCE 编译, 否则内部
 * strtol 调用自身也会被头文件重定向成自递归. */
#include <stdlib.h>

long __isoc23_strtol(const char* s, char** e, int b) { return strtol(s, e, b); }
__asm__(".symver __isoc23_strtol,__isoc23_strtol@GLIBC_2.38");

long long __isoc23_strtoll(const char* s, char** e, int b) { return strtoll(s, e, b); }
__asm__(".symver __isoc23_strtoll,__isoc23_strtoll@GLIBC_2.38");

unsigned long __isoc23_strtoul(const char* s, char** e, int b) { return strtoul(s, e, b); }
__asm__(".symver __isoc23_strtoul,__isoc23_strtoul@GLIBC_2.38");

unsigned long long __isoc23_strtoull(const char* s, char** e, int b) { return strtoull(s, e, b); }
__asm__(".symver __isoc23_strtoull,__isoc23_strtoull@GLIBC_2.38");

float __isoc23_strtof(const char* s, char** e) { return strtof(s, e); }
__asm__(".symver __isoc23_strtof,__isoc23_strtof@GLIBC_2.38");

double __isoc23_strtod(const char* s, char** e) { return strtod(s, e); }
__asm__(".symver __isoc23_strtod,__isoc23_strtod@GLIBC_2.38");

long double __isoc23_strtold(const char* s, char** e) { return strtold(s, e); }
__asm__(".symver __isoc23_strtold,__isoc23_strtold@GLIBC_2.38");
