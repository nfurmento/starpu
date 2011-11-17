/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010, 2011  Centre National de la Recherche Scientifique
 * Copyright (C) 2010, 2011  Université de Bordeaux 1
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

/* gcc build:

   gcc -fopenmp vector_scal.c -o vector_scal $(pkg-config --cflags libstarpu) $(pkg-config --libs libstarpu)

 */

#include <starpu.h>
#include <stdio.h>
#include <limits.h>

#define	NX	2048
#define FPRINTF(ofile, fmt, args ...) do { if (!getenv("STARPU_SSILENT")) {fprintf(ofile, fmt, ##args); }} while(0)

void scal_cpu_func(void *buffers[], void *_args) {
	unsigned i;
	float *factor = _args;
	starpu_vector_interface_t *vector = buffers[0];
	unsigned n = STARPU_VECTOR_GET_NX(vector);
	float *val = (float *)STARPU_VECTOR_GET_PTR(vector);

	FPRINTF(stderr, "running task with %d CPUs.\n", starpu_combined_worker_get_size());

#pragma omp parallel for num_threads(starpu_combined_worker_get_size())
	for (i = 0; i < n; i++)
		val[i] *= *factor;
}

static struct starpu_perfmodel vector_scal_model = {
	.type = STARPU_HISTORY_BASED,
	.symbol = "vector_scale_parallel"
};

static starpu_codelet cl = {
	.where = STARPU_CPU,
	.type = STARPU_FORKJOIN,
	.max_parallelism = INT_MAX,
	.cpu_func = scal_cpu_func,
	.nbuffers = 1,
	.model = &vector_scal_model,
};

int main(int argc, char **argv)
{
	struct starpu_conf conf;
	float vector[NX];
	unsigned i;
	for (i = 0; i < NX; i++)
                vector[i] = (i+1.0f);

	FPRINTF(stderr, "BEFORE: First element was %f\n", vector[0]);
	FPRINTF(stderr, "BEFORE: Last element was %f\n", vector[NX-1]);

	starpu_conf_init(&conf);

	/* Most OpenMP implementations do not support concurrent parallel
	 * sections, so only create one big worker */
	conf.single_combined_worker = 1;

	starpu_init(&conf);

	starpu_data_handle vector_handle;
	starpu_vector_data_register(&vector_handle, 0, (uintptr_t)vector, NX, sizeof(vector[0]));

	float factor = 3.14;

	struct starpu_task *task = starpu_task_create();
	task->synchronous = 1;

	task->cl = &cl;

	task->buffers[0].handle = vector_handle;
	task->buffers[0].mode = STARPU_RW;
	task->cl_arg = &factor;
	task->cl_arg_size = sizeof(factor);

	starpu_task_submit(task);
	starpu_data_unregister(vector_handle);

	starpu_task_destroy(task);

	/* terminate StarPU, no task can be submitted after */
	starpu_shutdown();

	FPRINTF(stderr, "AFTER: First element is %f\n", vector[0]);
	FPRINTF(stderr, "AFTER: Last element is %f\n", vector[NX-1]);

	return 0;
}
