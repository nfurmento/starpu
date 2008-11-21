#include "dw_factolu.h"

uint8_t *advance_12_21; /* size nblocks*nblocks */
uint8_t *advance_11; /* size nblocks*nblocks */
uint8_t *advance_22; /* array of nblocks *nblocks*nblocks */

tick_t start;
tick_t end;

#define STARTED	0x01
#define DONE	0x10

/*
 *	Upgraded Callbacks : break the pipeline design !
 */

void dw_callback_v2_codelet_update_u22(void *argcb)
{
	cl_args *args = argcb;	

	unsigned k = args->k;
	unsigned i = args->i;
	unsigned j = args->j;
	unsigned nblocks = args->nblocks;

	/* we did task 22k,i,j */
	advance_22[k*nblocks*nblocks + i + j*nblocks] = DONE;
	
	if ( (i == j) && (i == k+1)) {
		/* we now reduce the LU22 part (recursion appears there) */
		codelet *cl = malloc(sizeof(codelet));
		cl_args *u11arg = malloc(sizeof(cl_args));
	
		cl->cl_arg = u11arg;
		cl->core_func = dw_core_codelet_update_u11;
#ifdef USE_CUDA
		cl->cublas_func = dw_cublas_codelet_update_u11;
#endif
	
		job_t j = job_create();
			j->where = ANY;
			j->cb = dw_callback_v2_codelet_update_u11;
			j->argcb = u11arg;
			j->cl = cl;
			//j->cuda_cost_model = task_11_cost_cuda;
			//j->core_cost_model = task_11_cost_core;
			//j->cost_model = task_11_cost;
			j->model = &model_11;

			j->nbuffers = 1;
			j->buffers[0].state =
				get_sub_data(args->dataA, 2, k+1, k+1);
			j->buffers[0].mode = RW;
	
		u11arg->dataA = args->dataA;
		u11arg->i = k + 1;
		u11arg->nblocks = args->nblocks;
		u11arg->sem = args->sem;

		/* schedule the codelet */
		push_prio_task(j);
	}

	/* 11k+1 + 22k,k+1,j => 21 k+1,j */
	if ( i == k + 1) {
		uint8_t dep;
		/* 11 k+1*/
		dep = advance_11[(k+1)];
		if (dep & DONE) {
			/* try to push the job */
			uint8_t u = ATOMIC_OR(&advance_12_21[(k+1) + j*nblocks], STARTED);
				if ((u & STARTED) == 0) {
					/* we are the only one that should 
					 * launch that task */
					cl_args *u21a = malloc(sizeof(cl_args));
					codelet *cl21 = malloc(sizeof(codelet));
		
					cl21->cl_arg = u21a;
					cl21->core_func = 
						dw_core_codelet_update_u21;
#ifdef USE_CUDA
					cl21->cublas_func = 
						dw_cublas_codelet_update_u21;
#endif
					job_t j21 = job_create();
					j21->where = ANY;
					j21->cb = dw_callback_v2_codelet_update_u21;
					j21->argcb = u21a;
					j21->cl = cl21;
					j21->model = &model_21;
			
					u21a->i = k+1;
					u21a->k = j;
					u21a->nblocks = args->nblocks;
					u21a->dataA = args->dataA;
					u21a->sem = args->sem;

					j21->nbuffers = 2;
					j21->buffers[0].state = 
						get_sub_data(args->dataA, 2, u21a->i, u21a->i);
					j21->buffers[0].mode = R;
					j21->buffers[1].state =
						get_sub_data(args->dataA, 2, u21a->i, u21a->k);
					j21->buffers[1].mode = RW;
		
					push_task(j21);
				}
		}
	}

	/* 11k + 22k-1,i,k => 12 k,i */
	if (j == k + 1) {
		uint8_t dep;
		/* 11 k+1*/
		dep = advance_11[(k+1)];
		if (dep & DONE) {
			/* try to push the job */
			uint8_t u = ATOMIC_OR(&advance_12_21[(k+1)*nblocks + i], STARTED);
				 if ((u & STARTED) == 0) {
					/* we are the only one that should launch that task */
					codelet *cl12 = malloc(sizeof(codelet));
					cl_args *u12a = malloc(sizeof(cl_args));

					cl12->cl_arg = u12a;
					cl12->core_func = dw_core_codelet_update_u12;
		
					job_t j12 = job_create();
						j12->where = ANY;
						j12->cb = dw_callback_v2_codelet_update_u12;
						j12->argcb = u12a;
						j12->cl = cl12;
						j12->model = &model_12;

#ifdef USE_CUDA
					cl12->cublas_func = dw_cublas_codelet_update_u12;
#endif

		
					u12a->i = k+1;
					u12a->k = i;
					u12a->nblocks = args->nblocks;
					u12a->dataA = args->dataA;
					u12a->sem = args->sem;


					j12->nbuffers = 2;
					j12->buffers[0].state = get_sub_data(args->dataA, 2, u12a->i, u12a->i); 
					j12->buffers[0].mode = R;
					j12->buffers[1].state = get_sub_data(args->dataA, 2, u12a->k, u12a->i); 
					j12->buffers[1].mode = RW;
					
					push_task(j12);
	
				}
		}
	}

	free(args);
}

void dw_callback_v2_codelet_update_u12(void *argcb)
{
	cl_args *args = argcb;	

	/* now launch the update of LU22 */
	unsigned i = args->i;
	unsigned k = args->k;
	unsigned nblocks = args->nblocks;

	/* we did task 21i,k */
	advance_12_21[i*nblocks + k] = DONE;

	unsigned slicey;
	for (slicey = i+1; slicey < nblocks; slicey++)
	{
		/* can we launch 22 i,args->k,slicey ? */
		/* deps : 21 args->k, slicey */
		uint8_t dep;
		dep = advance_12_21[i + slicey*nblocks];
		if (dep & DONE)
		{
			/* perhaps we may schedule the 22 i,args->k,slicey task */
			uint8_t u = ATOMIC_OR(&advance_22[i*nblocks*nblocks + slicey*nblocks + k], STARTED);
                        if ((u & STARTED) == 0) {
				/* update that square matrix */
				cl_args *u22a = malloc(sizeof(cl_args));
				codelet *cl22 = malloc(sizeof(codelet));

				cl22->cl_arg = u22a;
				cl22->core_func = dw_core_codelet_update_u22;
#ifdef USE_CUDA
				cl22->cublas_func = dw_cublas_codelet_update_u22;
#endif

				job_t j22 = job_create();
				j22->where = ANY;
				j22->cb = dw_callback_v2_codelet_update_u22;
				j22->argcb = u22a;
				j22->cl = cl22;
				j22->model = &model_22;

				u22a->k = i;
				u22a->i = k;
				u22a->j = slicey;
				u22a->dataA = args->dataA;
				u22a->nblocks = nblocks;
				u22a->sem = args->sem;

				j22->nbuffers = 3;

				j22->buffers[0].state = get_sub_data(args->dataA, 2, u22a->i, u22a->k);
				j22->buffers[0].mode = R;

				j22->buffers[1].state = get_sub_data(args->dataA, 2, u22a->k, u22a->j);
				j22->buffers[1].mode = R;

				j22->buffers[2].state = get_sub_data(args->dataA, 2, u22a->i, u22a->j);
				j22->buffers[2].mode = RW;
				
				/* schedule that codelet */
				if (slicey == i+1) {
					push_prio_task(j22);
				}
				else {
					push_task(j22);
				}
			}
		}
	}
}

void dw_callback_v2_codelet_update_u21(void *argcb)
{
	cl_args *args = argcb;	

	/* now launch the update of LU22 */
	unsigned i = args->i;
	unsigned k = args->k;
	unsigned nblocks = args->nblocks;

	/* we did task 21i,k */
	advance_12_21[i + k*nblocks] = DONE;


	unsigned slicex;
	for (slicex = i+1; slicex < nblocks; slicex++)
	{
		/* can we launch 22 i,slicex,k ? */
		/* deps : 12 slicex k */
		uint8_t dep;
		dep = advance_12_21[i*nblocks + slicex];
		if (dep & DONE)
		{
			/* perhaps we may schedule the 22 i,args->k,slicey task */
			uint8_t u = ATOMIC_OR(&advance_22[i*nblocks*nblocks + k*nblocks + slicex], STARTED);
                        if ((u & STARTED) == 0) {
				/* update that square matrix */
				cl_args *u22a = malloc(sizeof(cl_args));
				codelet *cl22 = malloc(sizeof(codelet));

				cl22->cl_arg = u22a;
				cl22->core_func = dw_core_codelet_update_u22;
#ifdef USE_CUDA
				cl22->cublas_func = dw_cublas_codelet_update_u22;
#endif

				job_t j22 = job_create();
				j22->where = ANY;
				j22->cb = dw_callback_v2_codelet_update_u22;
				j22->argcb = u22a;
				j22->cl = cl22;
				j22->model = &model_22;

				u22a->k = i;
				u22a->i = slicex;
				u22a->j = k;
				u22a->dataA = args->dataA;
				u22a->nblocks = nblocks;
				u22a->sem = args->sem;

				j22->nbuffers = 3;

				j22->buffers[0].state = get_sub_data(args->dataA, 2, u22a->i, u22a->k);
				j22->buffers[0].mode = R;

				j22->buffers[1].state = get_sub_data(args->dataA, 2, u22a->k, u22a->j);
				j22->buffers[1].mode = R;

				j22->buffers[2].state = get_sub_data(args->dataA, 2, u22a->i, u22a->j);
				j22->buffers[2].mode = RW;
				
				/* schedule that codelet */
				if (slicex == i+1) {
					/* try to optimize the path to 11k+1 */
					push_prio_task(j22);
				}
				else {
					push_task(j22);
				}
			}
		}
	}
}

void dw_callback_v2_codelet_update_u11(void *argcb)
{
	/* in case there remains work, go on */
	cl_args *args = argcb;

	unsigned nblocks = args->nblocks;
	unsigned i = args->i;

	/* we did task 11k */
	advance_11[i] = DONE;

	if (i == nblocks - 1) 
	{
		/* we are done : wake the application up  */
		sem_post(args->sem);
		return;
	}
	else 
	{
		/* put new tasks */
		unsigned slice;
		for (slice = i + 1; slice < nblocks; slice++)
		{

			/* can we launch 12i,slice ? */
			uint8_t deps12;
			if (i == 0) {
				deps12 = DONE;
			}
			else {
				deps12 = advance_22[(i-1)*nblocks*nblocks + slice + i*nblocks];		
			}
			if (deps12 & DONE) {
				/* we may perhaps launch the task 12i,slice */
				 uint8_t u = ATOMIC_OR(&advance_12_21[i*nblocks + slice], STARTED);
				 if ((u & STARTED) == 0) {
					/* we are the only one that should launch that task */
					codelet *cl12 = malloc(sizeof(codelet));
					cl_args *u12a = malloc(sizeof(cl_args));

					cl12->cl_arg = u12a;
					cl12->core_func = dw_core_codelet_update_u12;
		
					job_t j12 = job_create();
						j12->where = ANY;
						j12->cb = dw_callback_v2_codelet_update_u12;
						j12->argcb = u12a;
						j12->cl = cl12;

						j12->model = &model_12;
#ifdef USE_CUDA
					cl12->cublas_func = dw_cublas_codelet_update_u12;
#endif

		
					u12a->i = i;
					u12a->k = slice;
					u12a->nblocks = args->nblocks;
					u12a->dataA = args->dataA;
					u12a->sem = args->sem;

					j12->nbuffers = 2;
					j12->buffers[0].state = get_sub_data(args->dataA, 2, u12a->i, u12a->i); 
					j12->buffers[0].mode = R;
					j12->buffers[1].state = get_sub_data(args->dataA, 2, u12a->k, u12a->i); 
					j12->buffers[1].mode = RW;

					if (slice == i +1) {
						push_prio_task(j12);
					}
					else {
						push_task(j12);
					}
	
				}
			}

			/* can we launch 21i,slice ? */
			if (i == 0) {
				deps12 = DONE;
			}
			else {
				deps12 = advance_22[(i-1)*nblocks*nblocks + slice*nblocks + i];		
			}
			if (deps12 & DONE) {
				/* we may perhaps launch the task 12i,slice */
				 uint8_t u = ATOMIC_OR(&advance_12_21[i + slice*nblocks], STARTED);
				 if ((u & STARTED) == 0) {
					/* we are the only one that should launch that task */
					cl_args *u21a = malloc(sizeof(cl_args));
					codelet *cl21 = malloc(sizeof(codelet));
		
					cl21->cl_arg = u21a;
					cl21->core_func = dw_core_codelet_update_u21;
#ifdef USE_CUDA
					cl21->cublas_func = dw_cublas_codelet_update_u21;
#endif
					job_t j21 = job_create();
						j21->where = ANY;
						j21->cb = dw_callback_v2_codelet_update_u21;
						j21->argcb = u21a;
						j21->cl = cl21;

						j21->model = &model_21;
		
					u21a->i = i;
					u21a->k = slice;
					u21a->nblocks = args->nblocks;
					u21a->dataA = args->dataA;
					u21a->sem = args->sem;

					j21->nbuffers = 2;
					j21->buffers[0].state = get_sub_data(args->dataA, 2, u21a->i, u21a->i);
					j21->buffers[0].mode = R;
					j21->buffers[1].state = get_sub_data(args->dataA, 2, u21a->i, u21a->k);
					j21->buffers[1].mode = RW;
		
					if (slice == i +1) {
						push_prio_task(j21);
					}
					else {
						push_task(j21);
					}
				}
			}
		}
	}
}



/*
 *	Callbacks 
 */


void dw_callback_codelet_update_u11(void *argcb)
{
	/* in case there remains work, go on */
	cl_args *args = argcb;

	if (args->i == args->nblocks - 1) 
	{
		/* we are done : wake the application up  */
		sem_post(args->sem);
		return;
	}
	else 
	{
		/* put new tasks */
		unsigned nslices;
		nslices = args->nblocks - 1 - args->i;

		unsigned *remaining = malloc(sizeof(unsigned));
		*remaining = 2*nslices; 

		unsigned slice;
		for (slice = args->i + 1; slice < args->nblocks; slice++)
		{

			/* update slice from u12 */
			cl_args *u12a = malloc(sizeof(cl_args));
			codelet *cl12 = malloc(sizeof(codelet));

			/* update slice from u21 */
			cl_args *u21a = malloc(sizeof(cl_args));
			codelet *cl21 = malloc(sizeof(codelet));

			cl12->cl_arg = u12a;
			cl21->cl_arg = u21a;
			cl12->core_func = dw_core_codelet_update_u12;
			cl21->core_func = dw_core_codelet_update_u21;
#ifdef USE_CUDA
			cl12->cublas_func = dw_cublas_codelet_update_u12;
			cl21->cublas_func = dw_cublas_codelet_update_u21;
#endif

			job_t j12 = job_create();
				j12->where = ANY;
				j12->cb = dw_callback_codelet_update_u12_21;
				j12->argcb = u12a;
				j12->cl = cl12;
				j12->model = &model_12;


			job_t j21 = job_create();
				j21->where = ANY;
				j21->cb = dw_callback_codelet_update_u12_21;
				j21->argcb = u21a;
				j21->cl = cl21;
				j21->model = &model_21;
			

			u12a->i = args->i;
			u12a->k = slice;
			u12a->nblocks = args->nblocks;
			u12a->dataA = args->dataA;
			u12a->remaining = remaining;
			u12a->sem = args->sem;
			
			u21a->i = args->i;
			u21a->k = slice;
			u21a->nblocks = args->nblocks;
			u21a->dataA = args->dataA;
			u21a->remaining = remaining;
			u21a->sem = args->sem;

			j12->nbuffers = 2;
			j12->buffers[0].state = 
				get_sub_data(args->dataA, 2, u12a->i, u12a->i); 
			j12->buffers[0].mode = R;
			j12->buffers[1].state = 
				get_sub_data(args->dataA, 2, u12a->k, u12a->i); 
			j12->buffers[1].mode = RW;

			j21->nbuffers = 2;
			j21->buffers[0].state = 
				get_sub_data(args->dataA, 2, u21a->i, u21a->i);
			j21->buffers[0].mode = R;
			j21->buffers[1].state = 
				get_sub_data(args->dataA, 2, u21a->i, u21a->k);
			j21->buffers[1].mode = RW;
		
			push_task(j12);
			push_task(j21);
		}
	}
}


void dw_callback_codelet_update_u22(void *argcb)
{
	cl_args *args = argcb;	

	if (ATOMIC_ADD(args->remaining, (-1)) == 0)
	{
		/* all worker already used the counter */
		free(args->remaining);

		/* we now reduce the LU22 part (recursion appears there) */
		codelet *cl = malloc(sizeof(codelet));
		cl_args *u11arg = malloc(sizeof(cl_args));
	
		cl->cl_arg = u11arg;
		cl->core_func = dw_core_codelet_update_u11;
#ifdef USE_CUDA
		cl->cublas_func = dw_cublas_codelet_update_u11;
#endif
	
		job_t j = job_create();
			j->where = ANY;
			j->cb = dw_callback_codelet_update_u11;
			j->argcb = u11arg;
			j->cl = cl;
			j->model = &model_11;

			j->nbuffers = 1;
			j->buffers[0].state = get_sub_data(args->dataA, 2, args->k + 1, args->k + 1);
			j->buffers[0].mode = RW;
	
		u11arg->dataA = args->dataA;
		u11arg->i = args->k + 1;
		u11arg->nblocks = args->nblocks;
		u11arg->sem = args->sem;

		/* schedule the codelet */
		push_task(j);

	}

	free(args);
}

void dw_callback_codelet_update_u12_21(void *argcb)
{
	cl_args *args = argcb;	

	if (ATOMIC_ADD(args->remaining, -1) == 0)
	{
		/* now launch the update of LU22 */
		unsigned i = args->i;
		unsigned nblocks = args->nblocks;

		/* the number of jobs to be done */
		unsigned *remaining = malloc(sizeof(unsigned));
		*remaining = (nblocks - 1 - i)*(nblocks - 1 - i);

		unsigned slicey, slicex;
		for (slicey = i+1; slicey < nblocks; slicey++)
		{
			for (slicex = i+1; slicex < nblocks; slicex++)
			{
				/* update that square matrix */
				cl_args *u22a = malloc(sizeof(cl_args));
				codelet *cl22 = malloc(sizeof(codelet));

				cl22->cl_arg = u22a;
				cl22->core_func = dw_core_codelet_update_u22;
#ifdef USE_CUDA
				cl22->cublas_func = dw_cublas_codelet_update_u22;
#endif

				job_t j22 = job_create();
				j22->where = ANY;
				j22->cb = dw_callback_codelet_update_u22;
				j22->argcb = u22a;
				j22->cl = cl22;
				j22->model = &model_22;

				u22a->k = i;
				u22a->i = slicex;
				u22a->j = slicey;
				u22a->dataA = args->dataA;
				u22a->nblocks = nblocks;
				u22a->remaining = remaining;
				u22a->sem = args->sem;

				j22->nbuffers = 3;

				j22->buffers[0].state = get_sub_data(args->dataA, 2, u22a->i, u22a->k);
				j22->buffers[0].mode = R;

				j22->buffers[1].state = get_sub_data(args->dataA, 2, u22a->k, u22a->j);
				j22->buffers[1].mode = R;

				j22->buffers[2].state = get_sub_data(args->dataA, 2, u22a->i, u22a->j);
				j22->buffers[2].mode = RW;
				

				
				/* schedule that codelet */
				push_task(j22);
			}
		}
	}
}



/*
 *	code to bootstrap the factorization 
 */

void dw_codelet_facto(data_state *dataA, unsigned nblocks)
{
	/* create a new codelet */
	codelet *cl = malloc(sizeof(codelet));
	cl_args *args = malloc(sizeof(cl_args));

	sem_t sem;

	sem_init(&sem, 0, 0U);

	args->sem = &sem;
	args->i = 0;
	args->nblocks = nblocks;
	args->dataA = dataA;

	cl->cl_arg = args;
	cl->core_func = dw_core_codelet_update_u11;
#ifdef USE_CUDA
	cl->cublas_func = dw_cublas_codelet_update_u11;
#endif

	GET_TICK(start);

	/* inject a new task with this codelet into the system */ 
	job_t j = job_create();
		j->where = ANY;
		j->cb = dw_callback_codelet_update_u11;
		j->argcb = args;
		j->cl = cl;
		j->model = &model_11;

		j->nbuffers = 1;
		j->buffers[0].state = get_sub_data(dataA, 2, 0, 0);
		j->buffers[0].mode = RW;

	/* schedule the codelet */
	push_task(j);

	/* stall the application until the end of computations */
	sem_wait(&sem);
	sem_destroy(&sem);
	GET_TICK(end);

	double timing = timing_delay(&start, &end);
	fprintf(stderr, "Computation took (in ms)\n");
	printf("%2.2f\n", timing/1000);

	unsigned n = get_blas_nx(dataA);
	double flop = (2.0f*n*n*n)/3.0f;
	fprintf(stderr, "Synthetic GFlops : %2.2f\n", (flop/timing/1000.0f));
}

void dw_codelet_facto_v2(data_state *dataA, unsigned nblocks)
{

	advance_11 = calloc(nblocks, sizeof(uint8_t));
	ASSERT(advance_11);

	advance_12_21 = calloc(nblocks*nblocks, sizeof(uint8_t));
	ASSERT(advance_12_21);

	advance_22 = calloc(nblocks*nblocks*nblocks, sizeof(uint8_t));
	ASSERT(advance_22);

	/* create a new codelet */
	codelet *cl = malloc(sizeof(codelet));
	cl_args *args = malloc(sizeof(cl_args));

	sem_t sem;

	sem_init(&sem, 0, 0U);

	args->sem = &sem;
	args->i = 0;
	args->nblocks = nblocks;
	args->dataA = dataA;

	cl->cl_arg = args;
	cl->core_func = dw_core_codelet_update_u11;
#ifdef USE_CUDA
	cl->cublas_func = dw_cublas_codelet_update_u11;
#endif

	GET_TICK(start);

	/* inject a new task with this codelet into the system */ 
	job_t j = job_create();
		j->where = ANY;
		j->cb = dw_callback_v2_codelet_update_u11;
		j->argcb = args;
		j->cl = cl;
		j->model = &model_11;
		j->nbuffers = 1;

		j->buffers[0].state = get_sub_data(dataA, 2, 0, 0); 
		j->buffers[0].mode = RW;

	/* schedule the codelet */
	push_task(j);

	/* stall the application until the end of computations */
	sem_wait(&sem);
	sem_destroy(&sem);

	/* gather all the data */
	unpartition_data(dataA, 0);

	GET_TICK(end);

	double timing = timing_delay(&start, &end);
	fprintf(stderr, "Computation took (in ms)\n");
	printf("%2.2f\n", timing/1000);

	unsigned n = get_blas_nx(dataA);
	double flop = (2.0f*n*n*n)/3.0f;
	fprintf(stderr, "Synthetic GFlops : %2.2f\n", (flop/timing/1000.0f));
}

void initialize_system(float **A, float **B, unsigned dim, unsigned pinned)
{
	init_machine();

	timing_init();

	if (pinned)
	{
		malloc_pinned(A, B, dim);
	} 
	else {
		*A = malloc(dim*dim*sizeof(float));
		*B = malloc(dim*sizeof(float));
	}
}

void dw_factoLU(float *matA, unsigned size, 
		unsigned ld, unsigned nblocks, 
		unsigned version)
{

#ifdef CHECK_RESULTS
	fprintf(stderr, "Checking results ...\n");
	float *Asaved;
	Asaved = malloc(ld*ld*sizeof(float));

	memcpy(Asaved, matA, ld*ld*sizeof(float));
#endif

	data_state dataA;

	/* monitor and partition the A matrix into blocks :
	 * one block is now determined by 2 unsigned (i,j) */
	monitor_blas_data(&dataA, 0, (uintptr_t)matA, ld, 
			size, size, sizeof(float));

	filter f;
		f.filter_func = block_filter_func;
		f.filter_arg = nblocks;

	filter f2;
		f2.filter_func = vertical_block_filter_func;
		f2.filter_arg = nblocks;

	map_filters(&dataA, 2, &f, &f2);

	switch (version) {
		case 1:
			dw_codelet_facto(&dataA, nblocks);
			break;
		default:
		case 2:
			dw_codelet_facto_v2(&dataA, nblocks);
			break;
	}

#ifdef CHECK_RESULTS
	compare_A_LU(Asaved, matA, size, ld);
#endif

	terminate_machine();
}
