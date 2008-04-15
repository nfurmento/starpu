#include "memalloc.h"

extern mem_node_descr descr;
static mem_chunk_list_t mc_list[MAXNODES];

void init_mem_chunk_lists(void)
{
	unsigned i;
	for (i = 0; i < MAXNODES; i++)
	{
		mc_list[i] = mem_chunk_list_new();
	}
}

void lock_all_subtree(data_state *data)
{
	if (data->nchildren == 0)
	{
		/* this is a leaf */	
		take_mutex(&data->header_lock);
	}
	else {
		/* lock all sub-subtrees children */
		int child;
		for (child = 0; child < data->nchildren; child++)
		{
			lock_all_subtree(&data->children[child]);
		}
	}
}

void unlock_all_subtree(data_state *data)
{
	if (data->nchildren == 0)
	{
		/* this is a leaf */	
		release_mutex(&data->header_lock);
	}
	else {
		/* lock all sub-subtrees children */
		int child;
		for (child = 0; child < data->nchildren; child++)
		{
			unlock_all_subtree(&data->children[child]);
		}
	}
}

unsigned may_free_subtree(data_state *data, unsigned node)
{
	if (data->nchildren == 0)
	{
		/* this is a leaf */	
		/* NO XXX TODO ! use an explicit refcnt per node  */
#warning crap !
		return !(is_rw_lock_referenced(&data->data_lock));
	}
	else {
		/* lock all sub-subtrees children */
		int child;
		for (child = 0; child < data->nchildren; child++)
		{
			unsigned res;
			res = may_free_subtree(&data->children[child], node);
			if (!res) return 0;
		}

		/* no problem was found */
		return 1;
	}
}

void do_free_mem_chunk(mem_chunk_t mc, unsigned node)
{
	/* remove the mem_chunk from the list */
	mem_chunk_list_erase(mc_list[node], mc);

	/* free the actual buffer */
	liberate_memory_on_node(mc->data, node);

	mem_chunk_delete(mc);
}

void transfer_subtree_to_node(data_state *data, unsigned src_node, unsigned dst_node)
{
	unsigned i,last;
	unsigned cnt;
	cache_state new_state;

	if (data->nchildren == 0)
	{
		/* this is a leaf */
		switch(data->per_node[src_node].state) {
			case OWNER:
				/* the local node has the only copy */
				/* the owner is now the destination_node */
				data->per_node[src_node].state = INVALID;
				data->per_node[dst_node].state = INVALID;

				driver_copy_data_1_to_1(data, src_node, dst_node, 0);

				break;
			case SHARED:
				/* some other node may have the copy */
				data->per_node[src_node].state = INVALID;

				/* count the number of copies */
				cnt = 0;
				for (i = 0; i < MAXNODES; i++)
				{
					if (data->per_node[i].state == SHARED) {
						cnt++; 
						last = i;
					}
				}

				if (cnt == 1)
					data->per_node[last].state = OWNER;

				break;
			case INVALID:
				/* nothing to be done */
				break;
			default:
				ASSERT(0);
				break;
		}
	}
	else {
		/* lock all sub-subtrees children */
		int child;
		for (child = 0; child < data->nchildren; child++)
		{
			transfer_subtree_to_node(&data->children[child], src_node, dst_node);
		}
	}
}

size_t try_to_free_mem_chunk(mem_chunk_t mc, unsigned node)
{
	data_state *data;

	data = mc->data;

	/* try to lock all the leafs of the subtree */
	lock_all_subtree(data);

	/* check if they are all "free" */
	if (may_free_subtree(data, node))
	{
		/* in case there was nobody using that buffer, throw it 
		 * away after writing it back to main memory */
		transfer_subtree_to_node(data, node, 0);

		/* now the actual buffer may be liberated */
		do_free_mem_chunk(mc, node);
	}

	/* unlock the leafs */
	unlock_all_subtree(data);

	return 0;
}

/* 
 * Try to free some memory on the specified node
 * 	returns 0 if no memory was released, 1 else
 */
size_t reclaim_memory(uint32_t node)
{
	printf("reclaim memory...\n");

	/* try to free all allocated data .. XXX */
	mem_chunk_t mc;
	for (mc = mem_chunk_list_begin(mc_list[node]);
	     mc != mem_chunk_list_end(mc_list[node]);
	     mc = mem_chunk_list_next(mc))
	{
		try_to_free_mem_chunk(mc, node);
	}

	return 0;
}

void register_mem_chunk(data_state *state, uint32_t dst_node, size_t size)
{
	mem_chunk_t mc = mem_chunk_new();

	mc->data = state;
	mc->size = size; 

	/* XXX TODO protect that structure ! */
	mem_chunk_list_push_front(mc_list[dst_node], mc);
}

void liberate_memory_on_node(data_state *state, uint32_t node)
{
	switch(descr.nodes[node]) {
		case RAM:
			free(state->per_node[node].ptr);
			break;
#ifdef USE_CUBLAS
		case CUBLAS_RAM:
			cublasFree(state->per_node[node].ptr);
			break;
#endif
		default:
			ASSERT(0);
	}

	state->per_node[node].allocated = 0;
	state->per_node[node].automatically_allocated = 0;
}

void allocate_memory_on_node(data_state *state, uint32_t dst_node)
{
	uintptr_t addr = 0;
	unsigned attempts = 0;

	do {
	//	printf("locate node = %d addr = %p dst_node =%d x %d y %d xy %d\n", get_local_memory_node(), (void *)addr, dst_node, state->nx, state->ny, state->nx*state->ny);
		switch(descr.nodes[dst_node]) {
			case RAM:
				addr = (uintptr_t) malloc(state->nx*state->ny*state->elemsize);
				break;
#ifdef USE_CUBLAS
			case CUBLAS_RAM:
				cublasAlloc(state->nx*state->ny, state->elemsize, (void **)&addr); 
				break;
#endif
			default:
				ASSERT(0);
		}

		if (!addr) {
			reclaim_memory(dst_node);
		}
		
	} while(!addr && attempts++ < 2);

	/* TODO handle capacity misses */
	ASSERT(addr);

	register_mem_chunk(state, dst_node, state->nx*state->ny*state->elemsize);

	state->per_node[dst_node].ptr = addr; 
	state->per_node[dst_node].ld = state->nx; 
	state->per_node[dst_node].allocated = 1;
	state->per_node[dst_node].automatically_allocated = 1;
}
