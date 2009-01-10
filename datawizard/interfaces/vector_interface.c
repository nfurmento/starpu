#include <common/util.h>
#include <datawizard/data_parameters.h>
#include <datawizard/coherency.h>
#include <datawizard/copy-driver.h>
#include <datawizard/hierarchy.h>

#include <common/hash.h>

#ifdef USE_CUDA
#include <cuda.h>
#endif

size_t allocate_vector_buffer_on_node(data_state *state, uint32_t dst_node);
void liberate_vector_buffer_on_node(data_state *state, uint32_t node);
int do_copy_vector_buffer_1_to_1(data_state *state, uint32_t src_node, uint32_t dst_node);
size_t dump_vector_interface(data_interface_t *interface, void *buffer);
size_t vector_interface_get_size(struct data_state_t *state);
uint32_t footprint_vector_interface_crc32(data_state *state, uint32_t hstate);
void display_vector_interface(data_state *state, FILE *f);
#ifdef USE_GORDON
int convert_vector_to_gordon(data_interface_t *interface, uint64_t *ptr, gordon_strideSize_t *ss); 
#endif

struct data_interface_ops_t interface_vector_ops = {
	.allocate_data_on_node = allocate_vector_buffer_on_node,
	.liberate_data_on_node = liberate_vector_buffer_on_node,
	.copy_data_1_to_1 = do_copy_vector_buffer_1_to_1,
	.dump_data_interface = dump_vector_interface,
	.get_size = vector_interface_get_size,
	.footprint = footprint_vector_interface_crc32,
#ifdef USE_GORDON
	.convert_to_gordon = convert_vector_to_gordon,
#endif
	.display = display_vector_interface
};

#ifdef USE_GORDON
int convert_vector_to_gordon(data_interface_t *interface, uint64_t *ptr, gordon_strideSize_t *ss) 
{
	ASSERT(gordon_interface);

	*ptr = (*interface).vector.ptr;
	(*ss).size = (*interface).vector.nx * (*interface).vector.elemsize;

	return 0;
}
#endif

/* declare a new data with the BLAS interface */
void monitor_vector_data(struct data_state_t *state, uint32_t home_node,
                        uintptr_t ptr, uint32_t nx, size_t elemsize)
{
	unsigned node;
	for (node = 0; node < MAXNODES; node++)
	{
		vector_interface_t *local_interface = &state->interface[node].vector;

		if (node == home_node) {
			local_interface->ptr = ptr;
		}
		else {
			local_interface->ptr = 0;
		}

		local_interface->nx = nx;
		local_interface->elemsize = elemsize;
	}

	state->interfaceid = BLAS_INTERFACE;
	state->ops = &interface_vector_ops;

	monitor_new_data(state, home_node);
}


static inline uint32_t footprint_vector_interface_generic(uint32_t (*hash_func)(uint32_t input, uint32_t hstate), data_state *state, uint32_t hstate)
{
	uint32_t hash;

	hash = hstate;
	hash = hash_func(get_vector_nx(state), hash);

	return hash;
}

uint32_t footprint_vector_interface_crc32(data_state *state, uint32_t hstate)
{
	return footprint_vector_interface_generic(crc32_be, state, hstate);
}

struct dumped_vector_interface_s {
	uintptr_t ptr;
	uint32_t nx;
	uint32_t elemsize;
} __attribute__ ((packed));

void display_vector_interface(data_state *state, FILE *f)
{
	vector_interface_t *interface;
	interface =  &state->interface[0].vector;

	fprintf(f, "%d\t", interface->nx);
}


size_t dump_vector_interface(data_interface_t *interface, void *_buffer)
{
	/* yes, that's DIRTY ... */
	struct dumped_vector_interface_s *buffer = _buffer;

	buffer->ptr = (*interface).vector.ptr;
	buffer->nx = (*interface).vector.nx;
	buffer->elemsize = (*interface).vector.elemsize;

	return (sizeof(struct dumped_vector_interface_s));
}

size_t vector_interface_get_size(struct data_state_t *state)
{
	size_t size;
	vector_interface_t *interface;

	interface =  &state->interface[0].vector;

	size = interface->nx*interface->elemsize;

	return size;
}

/* offer an access to the data parameters */
uint32_t get_vector_nx(data_state *state)
{
	return (state->interface[0].vector.nx);
}

uintptr_t get_vector_local_ptr(data_state *state)
{
	unsigned node;
	node = get_local_memory_node();

	ASSERT(state->per_node[node].allocated);

	return (state->interface[node].vector.ptr);
}

/* memory allocation/deallocation primitives for the vector interface */

/* returns the size of the allocated area */
size_t allocate_vector_buffer_on_node(data_state *state, uint32_t dst_node)
{
	uintptr_t addr;
	size_t allocated_memory;

	uint32_t nx = state->interface[dst_node].vector.nx;
	size_t elemsize = state->interface[dst_node].vector.elemsize;

	node_kind kind = get_node_kind(dst_node);

	switch(kind) {
		case RAM:
			addr = (uintptr_t)malloc(nx*elemsize);
			break;
#ifdef USE_CUDA
		case CUDA_RAM:
			cublasAlloc(nx, elemsize, (void **)&addr);
			break;
#endif
		default:
			assert(0);
	}

	if (addr) {
		/* allocation succeeded */
		allocated_memory = nx*elemsize;

		/* update the data properly in consequence */
		state->interface[dst_node].vector.ptr = addr;
	} else {
		/* allocation failed */
		allocated_memory = 0;
	}
	
	return allocated_memory;
}

void liberate_vector_buffer_on_node(data_state *state, uint32_t node)
{
	node_kind kind = get_node_kind(node);
	switch(kind) {
		case RAM:
			free((void*)state->interface[node].vector.ptr);
			break;
#ifdef USE_CUDA
		case CUDA_RAM:
			cublasFree((void*)state->interface[node].vector.ptr);
			break;
#endif
		default:
			assert(0);
	}
}

#ifdef USE_CUDA
static void copy_cublas_to_ram(data_state *state, uint32_t src_node, uint32_t dst_node)
{
	vector_interface_t *src_vector;
	vector_interface_t *dst_vector;

	src_vector = &state->interface[src_node].vector;
	dst_vector = &state->interface[dst_node].vector;

	cublasGetVector(src_vector->nx, src_vector->elemsize,
		(uint8_t *)src_vector->ptr, 1,
		(uint8_t *)dst_vector->ptr, 1);

	TRACE_DATA_COPY(src_node, dst_node, src_vector->nx*src_vector->elemsize);
}

static void copy_ram_to_cublas(data_state *state, uint32_t src_node, uint32_t dst_node)
{
	vector_interface_t *src_vector;
	vector_interface_t *dst_vector;

	src_vector = &state->interface[src_node].vector;
	dst_vector = &state->interface[dst_node].vector;

	cublasSetVector(src_vector->nx, src_vector->elemsize,
		(uint8_t *)src_vector->ptr, 1,
		(uint8_t *)dst_vector->ptr, 1);

	TRACE_DATA_COPY(src_node, dst_node, src_vector->nx*src_vector->elemsize);
}
#endif // USE_CUDA

static void dummy_copy_ram_to_ram(data_state *state, uint32_t src_node, uint32_t dst_node)
{
	uint32_t nx = state->interface[dst_node].vector.nx;
	size_t elemsize = state->interface[dst_node].vector.elemsize;

	uintptr_t ptr_src = state->interface[src_node].vector.ptr;
	uintptr_t ptr_dst = state->interface[dst_node].vector.ptr;

	memcpy((void *)ptr_dst, (void *)ptr_src, nx*elemsize);

	TRACE_DATA_COPY(src_node, dst_node, nx*elemsize);
}

int do_copy_vector_buffer_1_to_1(data_state *state, uint32_t src_node, uint32_t dst_node)
{
	node_kind src_kind = get_node_kind(src_node);
	node_kind dst_kind = get_node_kind(dst_node);

	switch (dst_kind) {
	case RAM:
		switch (src_kind) {
			case RAM:
				/* RAM -> RAM */
				 dummy_copy_ram_to_ram(state, src_node, dst_node);
				 break;
#ifdef USE_CUDA
			case CUDA_RAM:
				/* CUBLAS_RAM -> RAM */
				/* only the proper CUBLAS thread can initiate this ! */
				if (get_local_memory_node() == src_node)
				{
					/* only the proper CUBLAS thread can initiate this directly ! */
					copy_cublas_to_ram(state, src_node, dst_node);
				}
				else
				{
					/* put a request to the corresponding GPU */
					post_data_request(state, src_node, dst_node);
				}
				break;
#endif
			case SPU_LS:
				ASSERT(0); // TODO
				break;
			case UNUSED:
				printf("error node %d UNUSED\n", src_node);
			default:
				assert(0);
				break;
		}
		break;
#ifdef USE_CUDA
	case CUDA_RAM:
		switch (src_kind) {
			case RAM:
				/* RAM -> CUBLAS_RAM */
				/* only the proper CUBLAS thread can initiate this ! */
				ASSERT(get_local_memory_node() == dst_node);
				copy_ram_to_cublas(state, src_node, dst_node);
				break;
			case CUDA_RAM:
			case SPU_LS:
				ASSERT(0); // TODO 
				break;
			case UNUSED:
			default:
				ASSERT(0);
				break;
		}
		break;
#endif
	case SPU_LS:
		ASSERT(0); // TODO
		break;
	case UNUSED:
	default:
		assert(0);
		break;
	}

	return 0;
}

