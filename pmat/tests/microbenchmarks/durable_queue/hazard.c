#include <stdlib.h>

#include "hazard.h"
#include "cvector.h"
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

struct hazard {
	volatile bool in_use;
	size_t id;
	cvector_vector_type(void *) retired;
	void *owned[HAZARDS_PER_THREAD];
	void (*destructor)(void *);
	struct hazard *next;
};

struct hazard_list {
	struct hazard *head;
	volatile size_t size;
	void (*destructor)(void *, void *);
	void *arg;
};

static struct hazard_list *hazard_table = NULL;

static pthread_key_t tls;

#define MAX_HAZARD_POINTERS HAZARDS_PER_THREAD * MAX_THREADS

static void default_destructor(void *p, void *arg) {
	free(p);
}

__attribute__((constructor)) static void init_hazard_table(void) {
	hazard_table = malloc(sizeof(*hazard_table));
	hazard_table->head = NULL;
	hazard_table->destructor = default_destructor;
	hazard_table->arg = NULL;
	hazard_table->size = 0;
}

__attribute__((constructor)) static void init_tls_key(void) {
	pthread_key_create(&tls, NULL);
}

static bool cvector_contains(cvector_vector_type(void *) vec, void *item) {
	for (void **obj = cvector_begin(vec); obj != cvector_end(vec); obj++) {
		if (*obj == item) {
			return true;
		}
	}
	return false;
}

/*
	TODO: Major, please optimize this thing, I can't even tell it's complexity. I think
	O(N^3)? Regardless, it's awfully inefficient, and a hash table would at least make it O(N).
*/
__attribute__((destructor)) void destroy_hazard_table(void) {
	// struct hazard *prev_hp = NULL;
	// cvector_vector_type(void *) del_list;

	// for (struct hazard *hp = hazard_table->head; hp; hp = hp->next) {
	// 	free(prev_hp);
	// 	struct iterator *it = list_iterator(hp->retired);

	// 	for (int i = 0; i < HAZARDS_PER_THREAD; i++)
	// 		if (hp->owned[i] && !list_contains(del_list, hp->owned[i]))
	// 			list_add(del_list, hp->owned[i], NULL);

	// 	void *ptr;
	// 	while ((ptr = iterator_next(it)))
	// 		if (!list_contains(del_list, ptr))
	// 			list_add(del_list, ptr, NULL);
		
	// 	prev_hp = hp;
	// 	list_destroy(hp->retired, NULL);
	// 	free(it);
	// }
	// free(prev_hp);
	// list_destroy(del_list, hazard_table->destructor);
	// free(hazard_table);
	// pthread_key_delete(tls);
}

static void scan(struct hazard *hp) {
	cvector_vector_type(void *) private_list = NULL;
	/*
		Loop through the hazard pointers owned by each thread. Add them to the private if they
		are non-NULL, hence in use.
	*/
	for (struct hazard *tmp_hp = hazard_table->head; tmp_hp; tmp_hp = tmp_hp->next)
		for (int i = 0; i < HAZARDS_PER_THREAD; i++)
			if (tmp_hp->owned[i])
				cvector_push_back(private_list, tmp_hp->owned[i]);
	
	size_t arr_size = cvector_size(hp->retired);
	cvector_vector_type(void *) tmp_arr = NULL;
	cvector_grow(tmp_arr, cvector_size(hp->retired));
	memcpy(tmp_arr, hp->retired, cvector_size(hp->retired) * sizeof(void *));
	cvector_set_size(hp->retired, 0);
	
	/*
		Since each thread has it's own list of retired pointers, we check to see
		if this thread's "retired" is no longer in the list of hazard pointers, hence
		no longer in use and can be freed.
	*/
	for (int i = 0; i < arr_size; i++)
		if (cvector_contains(private_list, tmp_arr[i])) {
			cvector_push_back(hp->retired, tmp_arr[i]);
		} else {
			hazard_table->destructor(tmp_arr[i], hazard_table->arg);
		}

	cvector_free(private_list);
}

static void help_scan(struct hazard *hp) {
	for (struct hazard *tmp_hp = hazard_table->head; tmp_hp; tmp_hp = tmp_hp->next) {
		// If we fail to mark the hazard pointer as active, then it's already in use.
		if (tmp_hp->in_use || !__sync_bool_compare_and_swap(&tmp_hp->in_use, false, true)) 
			continue;

		void *data;
		while ((data = *(cvector_end(tmp_hp->retired) - 1))) {
			cvector_pop_back(tmp_hp->retired);
			cvector_push_back(hp->retired, data);
			
			if (cvector_size(hp->retired) >= MAX_HAZARD_POINTERS) 
				scan(hp);
		}

		tmp_hp->in_use = false;
	}
}

static struct hazard *create() {
	struct hazard *hp = malloc(sizeof(*hp));
	hp->in_use = true;	
	hp->retired = NULL;
	return hp;
}

static void init_tls_hp(void) {
	static volatile int index = 0;
	for (struct hazard *tmp_hp = hazard_table->head; tmp_hp; tmp_hp = tmp_hp->next) {
		if (tmp_hp->in_use || __sync_bool_compare_and_swap(&tmp_hp->in_use, false, true)) 
			continue;

		pthread_setspecific(tls, tmp_hp);	
		return;
	}
	
	struct hazard *hp = create();
	hp->id = index++;
	struct hazard *old_head;
	do {
		old_head = hazard_table->head;
		hp->next = old_head;
	} while (!__sync_bool_compare_and_swap(&hazard_table->head, old_head, hp));
	__sync_fetch_and_add(&hazard_table->size, HAZARDS_PER_THREAD);	
	pthread_setspecific(tls, hp);
}

bool hazard_acquire(unsigned int index, void *data) {	
	// Get the hazard pointer from thread-local storage if it is allocated.
	struct hazard *hp = pthread_getspecific(tls);
	// If it hasn't been allocated, then we allocate it here.
	if (!hp) {
		init_tls_hp();
		hp = pthread_getspecific(tls);
	}
	hp->owned[index] = data;	
	return true;
}

bool hazard_release_all(bool retire) {
	// Get the hazard pointer from thread-local storage if it is allocated.
	struct hazard *hp = pthread_getspecific(tls);

	for (int i = 0; i < HAZARDS_PER_THREAD; i++) {
		void *data = hp->owned[i];
		if (data) {
			hp->owned[i] = NULL;
			if (retire) {
				cvector_push_back(hp->retired, data);				
				if (cvector_size(hp->retired) >= HAZARDS_PER_THREAD) {
					scan(hp);					
					help_scan(hp);
				}
			}
		}
	}

	return true;
}

bool hazard_release(void *data, bool retire) {	
	// Get the hazard pointer from thread-local storage if it is allocated.
	struct hazard *hp = pthread_getspecific(tls);
	// If it hasn't been allocated, then surely the current thread never acquired anything.
	if (!hp)
		return false;
	
	for (int i = 0; i < HAZARDS_PER_THREAD; i++) {
		if (hp->owned[i] == data) {
			hp->owned[i] = NULL;
			if (retire) {
				cvector_push_back(hp->retired, data);
				if (cvector_size(hp->retired) >= HAZARDS_PER_THREAD) {
					scan(hp);
					help_scan(hp);
				}
			}
		}
	}

	return true;
}

bool hazard_register_destructor(void (*destructor)(void *, void *), void *arg) {	
	hazard_table->destructor = destructor;
	hazard_table->arg = arg;
	return true;
}
