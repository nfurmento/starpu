#ifndef __HIERARCHY_H__
#define __HIERARCHY_H__

#include <stdarg.h>
#include "coherency.h"
#include "memalloc.h"

typedef struct filter_t {
	unsigned (*filter_func)(struct filter_t *, data_state *); /* the actual partitionning function */
	uint32_t filter_arg;
} filter;

void monitor_new_data(data_state *state, uint32_t home_node);

void partition_data(data_state *initial_data, filter *f); 
void unpartition_data(data_state *root_data, uint32_t gathering_node);

void map_filter(data_state *root_data, filter *f);

/* unsigned list */
data_state *get_sub_data(data_state *root_data, unsigned depth, ... );

/* filter * list */
void map_filters(data_state *root_data, unsigned nfilters, ...);

#endif
