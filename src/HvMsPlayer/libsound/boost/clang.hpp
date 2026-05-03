//  (C) Copyright John Maddock 2001 - 2003.
//  (C) Copyright Darin Adler 2001 - 2002.
//  Use, modification and distribution are subject to the
//  Boost Software License, Version 1.0. (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org for most recent version.

//  Clang C++ compiler setup.
//  Clang emulates GCC, so we use the GCC configuration as a base.

#define BOOST_GCC_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#define BOOST_GCC BOOST_GCC_VERSION

#if defined(__GXX_EXPERIMENTAL_CXX0X__) || (__cplusplus >= 201103L)
#  define BOOST_GCC_CXX11
#endif

// Include GCC configuration for compatibility
#include "gcc.hpp"
