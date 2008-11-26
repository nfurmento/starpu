#ifndef __HIERARCHY_H__
#define __HIERARCHY_H__

#include <stdarg.h>
#include "coherency.h"
#include "memalloc.h"

struct data_state_t;

typedef struct filter_t {
	unsigned (*filter_func)(struct filter_t *, struct data_state_t *); /* the actual partitionning function */
	uint32_t filter_arg;
} filter;

void monitor_new_data(struct data_state_t *state, uint32_t home_node);
void delete_data(struct data_state_t *state);

void partition_data(struct data_state_t *initial_data, filter *f); 
void unpartition_data(struct data_state_t *root_data, uint32_t gathering_node);

void map_filter(struct data_state_t *root_data, filter *f);

/* unsigned list */
struct data_state_t *get_sub_data(struct data_state_t *root_data, unsigned depth, ... );

/* filter * list */
void map_filters(struct data_state_t *root_data, unsigned nfilters, ...);

#endif
