/*
 * StarPU
 * Copyright (C) INRIA 2009 (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR in PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include <math.h>
#include <pthread.h>
#include <sys/time.h>

#include <starpu.h>

#include "starpufft.h"

#define _FFTW_FLAGS FFTW_ESTIMATE

enum steps {
	TWIST1, FFT1, JOIN, TWIST2, FFT2, TWIST3, END
};

#define NUMBER_BITS 5
#define NUMBER_SHIFT (64 - NUMBER_BITS)
#define STEP_BITS 3
#define STEP_SHIFT (NUMBER_SHIFT - STEP_BITS)

#define I_BITS STEP_SHIFT

enum type {
	R2C,
	C2R,
	C2C
};

static unsigned task_per_worker[STARPU_NMAXWORKERS];
static unsigned samples_per_worker[STARPU_NMAXWORKERS];
static struct timeval start, submit_tasks, end;

/*
 *
 *	The actual kernels
 *
 */

struct STARPUFFT(plan) {
	int number;	/* uniquely identifies the plan, for starpu tags */

	int *n;
	int *n1;
	int *n2;
	int totsize;
	int totsize1;	/* Number of first-round tasks */
	int totsize2;	/* Size of first-round tasks */
	int totsize3;	/* Number of second-round tasks */
	int totsize4;	/* Size of second-round tasks */
	int dim;
	enum type type;
	int sign;

	STARPUFFT(complex) *roots[2];
	starpu_data_handle roots_handle[2];

	struct {
#ifdef USE_CUDA
		cufftHandle plan1_cuda, plan2_cuda;
		int initialized1, initialized2;
#endif
#ifdef HAVE_FFTW
		_fftw_plan plan1_cpu, plan2_cpu;
		_fftw_complex *in1, *out1;
		_fftw_complex *in2, *out2;
#endif
	} plans[STARPU_NMAXWORKERS];

#ifdef HAVE_FFTW
	_fftw_plan plan_gather;
#endif

	STARPUFFT(complex) *in, *twisted1, *fft1, *twisted2, *fft2, *out;

	starpu_data_handle *in_handle, *twisted1_handle, *fft1_handle, *twisted2_handle, *fft2_handle;
	struct starpu_task **twist1_tasks, **fft1_tasks, **twist2_tasks, **fft2_tasks, **twist3_tasks;
	struct starpu_task *join_task, *end_task;
	struct STARPUFFT(args) *fft1_args, *fft2_args;
};

struct STARPUFFT(args) {
	struct STARPUFFT(plan) *plan;
	int i, j, jj, kk, ll, *iv, *kkv;
};

static void
check_dims(STARPUFFT(plan) plan)
{
	int dim;
	for (dim = 0; dim < plan->dim; dim++)
		if (plan->n[dim] & (plan->n[dim]-1)) {
			fprintf(stderr,"can't cope with non-power-of-2\n");
			STARPU_ASSERT(0);
		}
}

static void
compute_roots(STARPUFFT(plan) plan)
{
	int dim, k;

	/* Compute the n-roots and m-roots of unity for twiddling */
	for (dim = 0; dim < plan->dim; dim++) {
		STARPUFFT(complex) exp = (plan->sign * 2. * 4.*atan(1.)) * _Complex_I / (STARPUFFT(complex)) plan->n[dim];
		plan->roots[dim] = malloc(plan->n[dim] * sizeof(**plan->roots));
		for (k = 0; k < plan->n[dim]; k++)
			plan->roots[dim][k] = cexp(exp*k);
		starpu_register_vector_data(&plan->roots_handle[dim], 0, (uintptr_t) plan->roots[dim], plan->n[dim], sizeof(**plan->roots));
	}
}

/* Empty task */
static void
STARPUFFT(void_kernel_cpu)(starpu_data_interface_t *descr, void *_args)
{
}

struct starpu_perfmodel_t STARPUFFT(void_model) = {
	.type = HISTORY_BASED,
	.symbol = TYPE"void"
};

static starpu_codelet STARPUFFT(void_codelet) = {
	.where = CORE,
	.core_func = STARPUFFT(void_kernel_cpu),
	.model = &STARPUFFT(void_model),
	.nbuffers = 0
};

#include "starpufftx1d.c"
#include "starpufftx2d.c"

starpu_tag_t
STARPUFFT(start)(STARPUFFT(plan) plan, void *_in, void *_out)
{
	starpu_tag_t tag;
	plan->in = _in;
	plan->out = _out;

	switch (plan->dim) {
		case 1: {
			switch (plan->type) {
			case C2C:
				tag = STARPUFFT(start1dC2C)(plan, _in, _out);
				break;
			default:
				STARPU_ASSERT(0);
				break;
			}
			break;
		}
		case 2:
			tag = STARPUFFT(start2dC2C)(plan, _in, _out);
			break;
		default:
			STARPU_ASSERT(0);
			break;
	}
	return tag;
}

void
STARPUFFT(execute)(STARPUFFT(plan) plan, void *in, void *out)
{
	gettimeofday(&start, NULL);
	memset(task_per_worker, 0, sizeof(task_per_worker));
	memset(samples_per_worker, 0, sizeof(task_per_worker));

	starpu_tag_t tag = STARPUFFT(start)(plan, in, out);
	gettimeofday(&submit_tasks, NULL);
	starpu_tag_wait(tag);

	gettimeofday(&end, NULL);
}

void
STARPUFFT(destroy_plan)(STARPUFFT(plan) plan)
{
	int workerid, dim, i;

	for (workerid = 0; workerid < starpu_get_worker_count(); workerid++) {
		switch (starpu_get_worker_type(workerid)) {
		case STARPU_CORE_WORKER:
#ifdef HAVE_FFTW
			_FFTW(free)(plan->plans[workerid].in1);
			_FFTW(free)(plan->plans[workerid].out1);
			_FFTW(destroy_plan)(plan->plans[workerid].plan1_cpu);
			_FFTW(free)(plan->plans[workerid].in2);
			_FFTW(free)(plan->plans[workerid].out2);
			_FFTW(destroy_plan)(plan->plans[workerid].plan2_cpu);
#endif
			break;
		case STARPU_CUDA_WORKER:
#ifdef USE_CUDA
			/* FIXME: Can't deallocate */
#endif
			break;
		default:
			STARPU_ASSERT(0);
			break;
		}
	}
	for (i = 0; i < plan->totsize1; i++) {
		starpu_delete_data(plan->twisted1_handle[i]);
		free(plan->twist1_tasks[i]);
		starpu_delete_data(plan->fft1_handle[i]);
		free(plan->fft1_tasks[i]);
	}

	free(plan->twisted1_handle);
	free(plan->twist1_tasks);
	free(plan->fft1_handle);
	free(plan->fft1_tasks);
	free(plan->fft1_args);

	free(plan->join_task);

	for (i = 0; i < plan->totsize3; i++) {
		starpu_delete_data(plan->twisted2_handle[i]);
		free(plan->twist2_tasks[i]);
		starpu_delete_data(plan->fft2_handle[i]);
		free(plan->fft2_tasks[i]);
		free(plan->twist3_tasks[i]);
	}

	free(plan->twisted2_handle);
	free(plan->twist2_tasks);
	free(plan->fft2_handle);
	free(plan->fft2_tasks);
	free(plan->twist3_tasks);
	free(plan->fft2_args);

	for (dim = 0; dim < plan->dim; dim++) {
		starpu_delete_data(plan->roots_handle[dim]);
		free(plan->roots[dim]);
	}

	switch (plan->dim) {
		case 1:
			STARPUFFT(free_1d_tags)(plan);
			break;
		case 2:
			STARPUFFT(free_2d_tags)(plan);
			break;
		default:
			STARPU_ASSERT(0);
			break;
	}

	free(plan->n);
	free(plan->n1);
	free(plan->n2);
	STARPUFFT(free)(plan->twisted1);
	STARPUFFT(free)(plan->fft1);
	STARPUFFT(free)(plan->twisted2);
	STARPUFFT(free)(plan->fft2);
#ifdef HAVE_FFTW
	_FFTW(destroy_plan)(plan->plan_gather);
#endif
	free(plan);
}

void *
STARPUFFT(malloc)(size_t n)
{
#ifdef USE_CUDA
	void *res;
	starpu_malloc_pinned_if_possible(&res, n);
	return res;
#else
#  ifdef HAVE_FFTW
	return _FFTW(malloc)(n);
#  else
	return malloc(n);
#  endif
#endif
}

void
STARPUFFT(free)(void *p)
{
#ifdef USE_CUDA
	// TODO: FIXME
#else
#  ifdef HAVE_FFTW
	_FFTW(free)(p);
#  else
	free(p);
#  endif
#endif
}

void
STARPUFFT(showstats)(FILE *out)
{
	int worker;
	unsigned total;

#define TIMING(begin,end) (double)((end.tv_sec - begin.tv_sec)*1000000 + (end.tv_usec - begin.tv_usec))
#define MSTIMING(begin,end) (TIMING(begin,end)/1000.)
	double paratiming = TIMING(start,end);
	fprintf(out, "Tasks submission took %2.2f ms\n", MSTIMING(start,submit_tasks));
	fprintf(out, "Tasks termination took %2.2f ms\n", MSTIMING(submit_tasks,end));

	fprintf(out, "Total %2.2f ms\n", MSTIMING(start,end));

	for (worker = 0, total = 0; worker < STARPU_NMAXWORKERS; worker++)
		total += task_per_worker[worker];

	for (worker = 0; worker < STARPU_NMAXWORKERS; worker++)
	{
		if (task_per_worker[worker])
		{
			char name[32];
			starpu_get_worker_name(worker, name, 32);

			unsigned long bytes = sizeof(STARPUFFT(complex))*samples_per_worker[worker];

			fprintf(stderr, "\t%s -> %2.2f MB\t%2.2f\tMB/s\t%u %2.2f %%\n", name, (1.0*bytes)/(1024*1024), bytes/paratiming, task_per_worker[worker], (100.0*task_per_worker[worker])/total);
		}
	}
}
