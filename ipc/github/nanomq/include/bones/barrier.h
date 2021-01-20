/*
   Copyright (C) 2007,2008 Qualcomm Incorporated. All rights reserved.
   Written by Max Krasnyansky <maxk@qualcomm.com>

   This file is part the Bones library. It is licensed under
   Boost Software License - Version 1.0 - August 17th, 2003

   Permission is hereby granted, free of charge, to any person or organization
   obtaining a copy of the software and accompanying documentation covered by
   this license (the "Software") to use, reproduce, display, distribute,
   execute, and transmit the Software, and to prepare derivative works of the
   Software, and to permit third-parties to whom the Software is furnished to
   do so, all subject to the following:

   The copyright notices in the Software and this entire statement, including
   the above license grant, this restriction and the following disclaimer,
   must be included in all copies of the Software, in whole or in part, and
   all derivative works of the Software, unless such copies or derivative
   works are solely in the form of machine-executable object code generated by
   a source language processor.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
   SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
   FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/

/**
 * @file bones/barrier.h
 * Compiler and memory barriers.
 */

#ifndef BONES2_BARRIER_H
#define BONES2_BARRIER_H

#include <bones/compiler.h>

namespace bones {
namespace barrier {

// We're using inline function here instead of #defines to avoid 
// name space clashes.

/**
 * Compiler barrier
 */
static inline void force_inline comp() { asm volatile("": : :"memory"); }

#if (defined(__i386__) || defined(__x86_64__))
#include <bones/barrier-x86.h>
#else
#error Unsupported CPU
#endif

} // namespace barrier
} // namespace bones

#endif // BONES2_BARRIER_H
