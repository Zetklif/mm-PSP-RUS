#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
#if !defined(LIBC_STDDEF_H) || defined(__need_wint_t) || defined(__need_size_t) || defined(__need_ptrdiff_t) || \
    defined(__need_NULL)
#if !defined(__need_wint_t) && !defined(__need_size_t) && !defined(__need_ptrdiff_t) && !defined(__need_NULL)
#define LIBC_STDDEF_H
#endif
#include_next <stddef.h>
#endif
#else

#ifndef LIBC_STDDEF_H
#define LIBC_STDDEF_H

#ifndef NULL
#define NULL ((void*)0)
#endif

#if !defined(_SIZE_T)
#define _SIZE_T
#if defined(_MIPS_SZLONG) && (_MIPS_SZLONG == 64)
typedef unsigned long size_t;
#else
typedef unsigned int  size_t;
#endif
#endif

typedef signed long ptrdiff_t;

#ifdef __GNUC__
#define offsetof(structure, member) __builtin_offsetof (structure, member)
#else
#define offsetof(structure, member) ((size_t)&(((structure*)0)->member))
#endif

#endif /* STDDEF_H */

#endif /* TARGET_PSP || PLATFORM_PSP */
