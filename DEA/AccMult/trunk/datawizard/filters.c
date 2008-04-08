#include "filters.h"

/*
 * an example of a dummy partition function : blocks ...
 */

unsigned block_filter_func(filter *f, data_state *root_data)
{
	unsigned nchunks;
	uint32_t arg = f->filter_arg;

	/* we will have arg chunks */
	nchunks = MIN(root_data->length, arg);
	
	/* first allocate the children data_state */
	root_data->children = malloc(nchunks*sizeof(data_state));
	ASSERT(root_data->children);

	/* actually create all the chunks */
	unsigned chunk;
	for (chunk = 0; chunk < nchunks; chunk++)
	{
		uint32_t chunk_size = (root_data->length + nchunks - 1)/nchunks;

		size_t offset = chunk*chunk_size;

		root_data->children[chunk].length = 
			MIN(chunk_size, root_data->length - offset);


		unsigned node;
		for (node = 0; node < MAXNODES; node++)
		{
			local_data_state *local = &root_data->children[chunk].per_node[node];
			if (root_data->per_node[node].allocated) {
				local->ptr = root_data->per_node[node].ptr + offset;
			}
		}
	}

	return nchunks;
}
