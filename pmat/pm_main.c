
/*--------------------------------------------------------------------*/
/*--- Persistent Memory Analysis Tool (PMAT).            pm_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of PMAT, the on-line persistent memory analysis tool
   plugin for Valgrind.

   Copyright (C) 2002-2017 Nicholas Nethercote
      njn@valgrind.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_libcassert.h"
#include <sys/types.h>
#include <unistd.h>

// Abstract representation of a pointer into a persistent segment.
// The first field, the identifier of the persistent file, refers
// to a user registered persistent segment. This tool must ensure
// at all times that the pm_segment_id is consistent across both
// the parent and child process. If this invariant is always upheld,
// the offset is just a relative offset; this is important in cases
// where the 'shadow heap' is mapped to some other offset.
struct persistent_ptr_t {
    Long pm_segment_id;
    Long offset;
};

static void pm_post_clo_init(void)
{
    // TODO: Need to find a way to setup a fd to communicate between processes!
    // Pipe does not seem to link inside of Valgrind!
    pid_t pid = VG_(fork)();
    if (pid == 0) {
        VG_(printf)("Child running and terminating...\n");
        VG_(exit(1));
    } else {
        VG_(printf)("Parent continuing...\n");
    }
}

static
IRSB* pm_instrument ( VgCallbackClosure* closure,
                      IRSB* bb,
                      const VexGuestLayout* layout, 
                      const VexGuestExtents* vge,
                      const VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
    return bb;
}

static void pm_fini(Int exitcode)
{
}

static void pm_pre_clo_init(void)
{
   VG_(details_name)            ("PMAT");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("the Persistent Memory Analysis Tool for Valgrind!");
   VG_(details_copyright_author)(
      "Copyright (C) 2019-, and GNU GPL'd, by Louis Jenkins.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);

   VG_(details_avg_translation_sizeB) ( 275 );

   VG_(basic_tool_funcs)        (pm_post_clo_init,
                                 pm_instrument,
                                 pm_fini);

   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(pm_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
