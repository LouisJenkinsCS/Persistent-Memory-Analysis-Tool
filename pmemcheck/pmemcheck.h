/*
 * Copyright (c) 2014-2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __PMEMCHECK_H
#define __PMEMCHECK_H


/* This file is for inclusion into client (your!) code.

   You can use these macros to manipulate and query memory permissions
   inside your own programs.

   See comment near the top of valgrind.h on how to use them.
*/

#include "valgrind.h"

#define PMAT_CACHELINE_SIZE 64

/* !! ABIWARNING !! ABIWARNING !! ABIWARNING !! ABIWARNING !!
   This enum comprises an ABI exported by Valgrind to programs
   which use client requests.  DO NOT CHANGE THE ORDER OF THESE
   ENTRIES, NOR DELETE ANY -- add new ones at the end. */
typedef
   enum {
       VG_USERREQ__PMC_DO_FLUSH = VG_USERREQ_TOOL_BASE('P','C'),
       VG_USERREQ__PMC_DO_FENCE,
       VG_USERREQ__PMC_RESERVED1,  /* Do not use. */
       VG_USERREQ__PMC_WRITE_STATS,
       VG_USERREQ__PMC_RESERVED2,  /* Do not use. */
       VG_USERREQ__PMC_RESERVED3,  /* Do not use. */
       VG_USERREQ__PMC_RESERVED4,  /* Do not use. */
       VG_USERREQ__PMC_RESERVED5,  /* Do not use. */
       VG_USERREQ__PMC_RESERVED7,  /* Do not use. */
       VG_USERREQ__PMC_RESERVED8,  /* Do not use. */
       VG_USERREQ__PMC_RESERVED6,  /* Do not use. */
       VG_USERREQ__PMC_PMAT_REGISTER,
       VG_USERREQ__PMC_PMAT_FORCE_SIMULATE_CRASH,
       VG_USERREQ__PMC_PMAT_CRASH_DISABLE,
       VG_USERREQ__PMC_PMAT_CRASH_ENABLE,
       VG_USERREQ__PMC_PMAT_TRANSIENT
   } Vg_PMemCheckClientRequest;


/* 
    Return value that should be used to signify a failure; differentiates
    an unexpected error and an assertion that the file is bad (I.E segmentation
    fault due to verification function or if it fails an assertion) 
*/
#define PMAT_VERIFICATION_FAILURE (0xBD)

/* Client-code macros to manipulate pmem mappings */

/** Register a CLFLUSH-like operation */
#define VALGRIND_PMC_DO_FLUSH(_qzz_addr,_qzz_len)                           \
    VALGRIND_DO_CLIENT_REQUEST_EXPR(0 /* default return */,                 \
                            VG_USERREQ__PMC_DO_FLUSH,                       \
                            (_qzz_addr), (_qzz_len), 0, 0, 0)

/** Register an SFENCE */
#define VALGRIND_PMC_DO_FENCE                                               \
    VALGRIND_DO_CLIENT_REQUEST_STMT(VG_USERREQ__PMC_DO_FENCE,               \
                                    0, 0, 0, 0, 0)

/** Forces a simulated crash and starts recovery */
#define PMAT_FORCE_CRASH() \
    VALGRIND_DO_CLIENT_REQUEST_STMT(VG_USERREQ__PMC_PMAT_FORCE_SIMULATE_CRASH,  \
                                    0, 0, 0, 0, 0)

/** Register a verification function to a particular mapping */
#define PMAT_REGISTER(_qzz_name, _qzz_addr, _qzz_size) \
    VALGRIND_DO_CLIENT_REQUEST_STMT(VG_USERREQ__PMC_PMAT_REGISTER, \
            (_qzz_name), (_qzz_addr), (_qzz_size), 0, 0)

/** Disable simulated crashes */
#define PMAT_CRASH_DISABLE() \
    VALGRIND_DO_CLIENT_REQUEST_STMT(VG_USERREQ__PMC_PMAT_CRASH_DISABLE, \
            0, 0, 0, 0, 0)

/** Enable simulated crashes */
#define PMAT_CRASH_ENABLE() \
    VALGRIND_DO_CLIENT_REQUEST_STMT(VG_USERREQ__PMC_PMAT_CRASH_ENABLE, \
            0, 0, 0, 0, 0)

// TODO: Make this an EXPR instead so that it can return if it succeeded!
/** Inform PMAT to ignore this address (marked as being transient) */
#define PMAT_TRANSIENT(_qzz_addr, _qzz_sz) \
    VALGRIND_DO_CLIENT_REQUEST_STMT(VG_USERREQ__PMC_PMAT_TRANSIENT, \
            (_qzz_addr), (_qzz_sz), 0, 0, 0)

#endif
