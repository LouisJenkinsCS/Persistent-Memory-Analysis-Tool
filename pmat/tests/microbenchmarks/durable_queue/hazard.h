#pragma once

#include "config.h"
#include <stdbool.h>

/*
	Tags the pointer, ptr, as being in-use, at the given index, and hence will not be
	freed until no further references exist of it.
*/
bool hazard_acquire(unsigned int index, void *ptr);

/*
	Registers a destructor which will be called for each ptr after they are
	no longer referenced by any threads. By default, free will be called.
*/
bool hazard_register_destructor(void (*destructor)(void *, void *), void *arg);

/*
	Releases the ptr, and is thereby free to be retired if retire is passed as
	true. A retired pointer is added to an internal list which keeps track of other
	retired pointers. When there no longer is a reference to a retired pointer, it will
	be freed using the hazard table's destructor.
*/
bool hazard_release(void *data, bool retire);

/*
	Releases all ptrs, much like c_utils_hazard_release.
*/
bool hazard_release_all(bool retire);
