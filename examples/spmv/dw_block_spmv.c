#include "dw_block_spmv.h"
#include "matrix-market/mm_to_bcsr.h"

tick_t start,end;
sem_t sem;

unsigned c = 256;
unsigned r = 256;

unsigned remainingtasks = -1;

data_handle sparse_matrix;
data_handle vector_in, vector_out;

uint32_t size;
char *inputfile;
bcsr_t *bcsr_matrix;

float *vector_in_ptr;
float *vector_out_ptr;

void create_data(void)
{
	/* read the input file */
	bcsr_matrix = mm_file_to_bcsr(inputfile, c, r);

	/* declare the corresponding block CSR to the runtime */
	monitor_bcsr_data(&sparse_matrix, 0, bcsr_matrix->nnz_blocks, bcsr_matrix->nrows_blocks,
	                (uintptr_t)bcsr_matrix->val, bcsr_matrix->colind, bcsr_matrix->rowptr, 
			0, bcsr_matrix->r, bcsr_matrix->c, sizeof(float));

	size = c*r*get_bcsr_nnz(sparse_matrix);
//	printf("size = %d \n ", size);

	/* initiate the 2 vectors */
	vector_in_ptr = malloc(size*sizeof(float));
	assert(vector_in_ptr);

	vector_out_ptr = malloc(size*sizeof(float));
	assert(vector_out_ptr);

	/* fill those */
	unsigned ind;
	for (ind = 0; ind < size; ind++)
	{
		vector_in_ptr[ind] = 2.0f;
		vector_out_ptr[ind] = 0.0f;
	}

	monitor_vector_data(&vector_in, 0, (uintptr_t)vector_in_ptr, size, sizeof(float));
	monitor_vector_data(&vector_out, 0, (uintptr_t)vector_out_ptr, size, sizeof(float));
}

void init_problem_callback(void *arg)
{
	unsigned *remaining = arg;

	unsigned val = ATOMIC_ADD(remaining, -1);

//	if (val < 10)
//		printf("callback %d remaining \n", val);

	if ( val == 0 )
	{
		printf("DONE ...\n");
		GET_TICK(end);

//		unpartition_data(sparse_matrix, 0);
		unpartition_data(vector_out, 0);

		sem_post(&sem);
	}
}


void call_filters(void)
{

	filter bcsr_f;
	filter vector_in_f, vector_out_f;

	bcsr_f.filter_func    = canonical_block_filter_bcsr;

	vector_in_f.filter_func = block_filter_func_vector;
	vector_in_f.filter_arg  = size/c;
	
	vector_out_f.filter_func = block_filter_func_vector;
	vector_out_f.filter_arg  = size/r;

	partition_data(sparse_matrix, &bcsr_f);

	partition_data(vector_in, &vector_in_f);
	partition_data(vector_out, &vector_out_f);
}

#define NSPMV	32
unsigned totaltasks;

codelet cl = {
	.where = CORE|CUBLAS,
	.core_func =  core_block_spmv,
#ifdef USE_CUDA
	.cublas_func = cublas_block_spmv,
#endif
	.nbuffers = 3
};

void launch_spmv_codelets(void)
{
	struct starpu_task *task_tab;
	uint8_t *is_entry_tab;

	/* we call one codelet per block */
	unsigned nblocks = get_bcsr_nnz(sparse_matrix); 
	unsigned nrows = get_bcsr_nrow(sparse_matrix); 

	remainingtasks = NSPMV*nblocks;
	totaltasks = remainingtasks;

	unsigned taskid = 0;

	task_tab = malloc(totaltasks*sizeof(struct starpu_task));
	STARPU_ASSERT(task_tab);

	is_entry_tab = calloc(totaltasks, sizeof(uint8_t));
	STARPU_ASSERT(is_entry_tab);

	printf("there will be %d codelets\n", remainingtasks);

	uint32_t *rowptr = get_bcsr_local_rowptr(sparse_matrix);
	uint32_t *colind = get_bcsr_local_colind(sparse_matrix);

	GET_TICK(start);

	unsigned loop;
	for (loop = 0; loop < NSPMV; loop++)
	{
		unsigned row;
		unsigned part = 0;

		for (row = 0; row < nrows; row++)
		{
			unsigned index;

			if (rowptr[row] == rowptr[row+1])
			{
				continue;
			}


			for (index = rowptr[row]; index < rowptr[row+1]; index++, part++)
			{
				struct starpu_task *task = &task_tab[taskid];

				task->use_tag = 1;
				task->tag_id = taskid;

				task->callback_func = init_problem_callback;
				task->callback_arg = &remainingtasks;
				task->cl = &cl;
				task->cl_arg = NULL;

				unsigned i = colind[index];
				unsigned j = row;
		
				task->buffers[0].state = get_sub_data(sparse_matrix, 1, part);
				task->buffers[0].mode  = R;
				task->buffers[1].state = get_sub_data(vector_in, 1, i);
				task->buffers[1].mode = R;
				task->buffers[2].state = get_sub_data(vector_out, 1, j);
				task->buffers[2].mode = RW;

				/* all tasks in the same row are dependant so that we don't wait too much for data 
				 * we need to wait on the previous task if we are not the first task of a row */
				if (index != rowptr[row & ~0x3])
				{
					/* this is not the first task in the row */
					tag_declare_deps(taskid, 1, taskid-1);

					is_entry_tab[taskid] = 0;
				}
				else {
					/* this is an entry task */
					is_entry_tab[taskid] = 1;
				}

				taskid++;
			}
		}
	}

	printf("start submitting tasks !\n");

	/* submit ALL tasks now */
	unsigned nchains = 0;
	unsigned task;
	for (task = 0; task < totaltasks; task++)
	{
		if (is_entry_tab[task]) {
			nchains++;
		}

		submit_task(&task_tab[task]);
	}

	printf("end of task submission (there was %d chains for %d tasks : ratio %d tasks per chain) !\n", nchains, totaltasks, totaltasks/nchains);
}

void init_problem(void)
{
	/* create the sparse input matrix */
	create_data();

	/* create a new codelet that will perform a SpMV on it */
	call_filters();
}

void print_results(void)
{
	unsigned row;

	for (row = 0; row < MIN(size, 16); row++)
	{
		printf("%2.2f\t%2.2f\n", vector_in_ptr[row], vector_out_ptr[row]);
	}
}

int main(__attribute__ ((unused)) int argc,
	__attribute__ ((unused)) char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage : %s filename [tile size]\n", argv[0]);
		exit(-1);
	}

	if (argc == 3)
	{
		/* third argument is the tile size */
		char *argptr;
		r = strtol(argv[2], &argptr, 10);
		c = r;
	}

	inputfile = argv[1];

	timing_init();

	/* start the runtime */
	init_machine();

	sem_init(&sem, 0, 0U);

	init_problem();

	launch_spmv_codelets();

	sem_wait(&sem);
	sem_destroy(&sem);

	print_results();

	double totalflop = 2.0*c*r*totaltasks;

	double timing = timing_delay(&start, &end);
	fprintf(stderr, "Computation took (in ms)\n");
	printf("%2.2f\n", timing/1000);
	fprintf(stderr, "Flop %e\n", totalflop);
	fprintf(stderr, "GFlops : %2.2f\n", totalflop/timing/1000);

	return 0;
}
