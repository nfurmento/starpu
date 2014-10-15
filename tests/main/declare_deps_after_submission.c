/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2011  Université de Bordeaux
 * Copyright (C) 2010, 2011, 2012, 2013  Centre National de la Recherche Scientifique
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

#include <stdio.h>
#include <unistd.h>

#include <starpu.h>
#include "../helper.h"

#ifdef STARPU_QUICK_CHECK
  #define NLOOPS	4
#else
  #define NLOOPS	128
#endif

void dummy_func(void *descr[] STARPU_ATTRIBUTE_UNUSED, void *arg STARPU_ATTRIBUTE_UNUSED)
{
}

static struct starpu_codelet dummy_codelet = 
{
	.cpu_funcs = {dummy_func, NULL},
	.cuda_funcs = {dummy_func, NULL},
	.opencl_funcs = {dummy_func, NULL},
	.cpu_funcs_name = {"dummy_func", NULL},
	.model = NULL,
	.nbuffers = 0
};

static struct starpu_task *create_dummy_task(void)
{
	struct starpu_task *task = starpu_task_create();

	task->cl = &dummy_codelet;
	task->cl_arg = NULL;

	return task;
}

int main(int argc, char **argv)
{
	int ret;
	unsigned loop, nloops = NLOOPS;

	ret = starpu_initialize(NULL, &argc, &argv);
	if (ret == -ENODEV) return STARPU_TEST_SKIPPED;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

	struct starpu_task *taskA, *taskB;

	for (loop = 0; loop < nloops; loop++)
	{
		taskA = create_dummy_task();
		taskB = create_dummy_task();

		/* By default, dynamically allocated tasks are destroyed at
		 * termination, we cannot declare a dependency on something
		 * that does not exist anymore. */
		taskA->destroy = 0;
		taskA->detach = 0;

		/* we wait for the tasks explicitly */
		taskB->detach = 0;

		ret = starpu_task_submit(taskA);
		if (ret == -ENODEV)
			return STARPU_TEST_SKIPPED;
		STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

		starpu_task_declare_deps_array(taskB, 1, &taskA);

		ret = starpu_task_submit(taskB);
		if (ret == -ENODEV)
			return STARPU_TEST_SKIPPED;
		STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

		ret = starpu_task_wait(taskB);
		STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

		ret = starpu_task_wait(taskA);
		STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

		starpu_task_destroy(taskA);
	}

	ret = starpu_task_wait_for_all();
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_wait_for_all");

	starpu_shutdown();

	return EXIT_SUCCESS;
}
