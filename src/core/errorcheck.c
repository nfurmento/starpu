/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010, 2014  Université de Bordeaux 1
 * Copyright (C) 2010, 2011  Centre National de la Recherche Scientifique
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

#include <core/errorcheck.h>
#include <core/workers.h>

void _starpu_set_worker_status(struct _starpu_worker *worker, enum _starpu_worker_status st)
{
	starpu_pthread_mutex_t *sched_mutex;
	starpu_pthread_cond_t *sched_cond;
	starpu_worker_get_sched_condition(worker->workerid, &sched_mutex, &sched_cond);
	STARPU_PTHREAD_MUTEX_LOCK(sched_mutex);
	worker->status = st;
	STARPU_PTHREAD_MUTEX_UNLOCK(sched_mutex);
}

void _starpu_set_local_worker_status(enum _starpu_worker_status st)
{
	struct _starpu_worker *worker = _starpu_get_local_worker_key();

	/* It is possible that we call this function from the application (and
	 * thereforce outside a worker), for instance if we are executing the
	 * callback function of a task with a "NULL" codelet. */
	if (worker)
		_starpu_set_worker_status(worker, st);
}

enum _starpu_worker_status _starpu_get_local_worker_status(void)
{
	struct _starpu_worker *worker = _starpu_get_local_worker_key();
	if (STARPU_UNLIKELY(!worker))
		return STATUS_INVALID;

	return worker->status;
}

/* It is forbidden to call blocking operations with Callback and during the
 * execution of a task. */
unsigned _starpu_worker_may_perform_blocking_calls(void)
{
	enum _starpu_worker_status st = _starpu_get_local_worker_status();

	return ( !(st == STATUS_CALLBACK) && !(st == STATUS_EXECUTING));
}
