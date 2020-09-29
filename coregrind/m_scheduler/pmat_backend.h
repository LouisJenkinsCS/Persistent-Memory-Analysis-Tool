#ifndef PMAT_BACKEND
#define PMAT_BACKEND

#include "pub_core_basics.h"

#define PMAT_BACKEND_MAX_THREADS 1024

/*
    Contains variables that will be written to and read from by PMAT if being used to
    communicate with the scheduler. 
*/

// Number of NVM Instructions executed.
UInt VG_(nvm_isns)[PMAT_BACKEND_MAX_THREADS];
__thread UInt VG_(curr_nvm_isns) = 0;

/* If False, a fault is Valgrind-internal (ie, a bug) */
Bool VG_(in_generated_code) = False;

/* Determines whether we use a static quantum or randomized version. */
Bool VG_(randomize_quantum) = False;
/* Randomized Seed for quantum. */
UInt VG_(quantum_seed) = 0;
/* If we are inside of code of interest as specified via a hint. */
Bool VG_(code_of_interest)[PMAT_BACKEND_MAX_THREADS] = {False};
/* If we handle code of interest hints.*/
Bool VG_(handle_code_of_interest) = False; 

/* Defines the thread-scheduling timeslice, in terms of the number of
   basic blocks we attempt to run each thread for.  Smaller values
   give finer interleaving but much increased scheduling overheads. */
UInt VG_(scheduling_quantum) = 1000;


#endif