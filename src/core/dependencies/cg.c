/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2012  Université de Bordeaux 1
 * Copyright (C) 2010, 2011  Centre National de la Recherche Scientifique
 * Copyright (C) 2012 inria
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

#include <starpu.h>
#include <common/config.h>
#include <common/utils.h>
#include <core/jobs.h>
#include <core/dependencies/cg.h>
#include <core/dependencies/tags.h>

void _starpu_cg_list_init(struct _starpu_cg_list *list)
{
	list->nsuccs = 0;
	list->ndeps = 0;
	list->ndeps_completed = 0;

#ifdef STARPU_DYNAMIC_DEPS_SIZE
	/* this is a small initial default value ... may be changed */
	list->succ_list_size = 0;
	list->succ =
		(struct _starpu_cg **) realloc(NULL, list->succ_list_size*sizeof(struct _starpu_cg *));
#endif
}

void _starpu_cg_list_deinit(struct _starpu_cg_list *list)
{
	unsigned id;
	for (id = 0; id < list->nsuccs; id++)
	{
		struct _starpu_cg *cg = list->succ[id];

		/* We remove the reference on the completion group, and free it
		 * if there is no more reference. */
		unsigned ntags = STARPU_ATOMIC_ADD(&cg->ntags, -1);
		if (ntags == 0)
			free(list->succ[id]);
	}

#ifdef STARPU_DYNAMIC_DEPS_SIZE
	free(list->succ);
#endif
}

void _starpu_add_successor_to_cg_list(struct _starpu_cg_list *successors, struct _starpu_cg *cg)
{
	STARPU_ASSERT(cg);

	/* where should that cg should be put in the array ? */
	unsigned index = STARPU_ATOMIC_ADD(&successors->nsuccs, 1) - 1;

#ifdef STARPU_DYNAMIC_DEPS_SIZE
	if (index >= successors->succ_list_size)
	{
		/* the successor list is too small */
		if (successors->succ_list_size > 0)
			successors->succ_list_size *= 2;
		else
			successors->succ_list_size = 4;

		/* NB: this is thread safe as the tag->lock is taken */
		successors->succ = (struct _starpu_cg **) realloc(successors->succ,
			successors->succ_list_size*sizeof(struct _starpu_cg *));
	}
#else
	STARPU_ASSERT(index < STARPU_NMAXDEPS);
#endif
	successors->succ[index] = cg;
}

/* Note: in case of a tag, it must be already locked */
void _starpu_notify_cg(struct _starpu_cg *cg)
{
	STARPU_ASSERT(cg);
	unsigned remaining = STARPU_ATOMIC_ADD(&cg->remaining, -1);

	if (remaining == 0)
	{
		cg->remaining = cg->ntags;

		struct _starpu_tag *tag;
		struct _starpu_cg_list *tag_successors, *job_successors;
		struct _starpu_job *j;

		/* the group is now completed */
		switch (cg->cg_type)
		{
			case STARPU_CG_APPS:
			{
				/* this is a cg for an application waiting on a set of
	 			 * tags, wake the thread */
				_STARPU_PTHREAD_MUTEX_LOCK(&cg->succ.succ_apps.cg_mutex);
				cg->succ.succ_apps.completed = 1;
				_STARPU_PTHREAD_COND_SIGNAL(&cg->succ.succ_apps.cg_cond);
				_STARPU_PTHREAD_MUTEX_UNLOCK(&cg->succ.succ_apps.cg_mutex);
				break;
			}

			case STARPU_CG_TAG:
			{
				tag = cg->succ.tag;
				tag_successors = &tag->tag_successors;

				tag_successors->ndeps_completed++;

				/* Note: the tag is already locked by the
				 * caller. */
				if ((tag->state == STARPU_BLOCKED) &&
					(tag_successors->ndeps == tag_successors->ndeps_completed))
				{
					/* reset the counter so that we can reuse the completion group */
					tag_successors->ndeps_completed = 0;
					_starpu_tag_set_ready(tag);
				}
				break;
			}

 		        case STARPU_CG_TASK:
			{
				j = cg->succ.job;

				job_successors = &j->job_successors;

				unsigned ndeps_completed =
					STARPU_ATOMIC_ADD(&job_successors->ndeps_completed, 1);

				if (job_successors->ndeps == ndeps_completed)
				{
					/* Note that this also ensures that tag deps are
					 * fulfilled. This counter is reseted only when the
					 * dependencies are are all fulfilled) */
					_starpu_enforce_deps_and_schedule(j, 1);
				}

				break;
			}

			default:
				STARPU_ABORT();
		}
	}
}

void _starpu_notify_cg_list(struct _starpu_cg_list *successors)
{
	unsigned nsuccs;
	unsigned succ;

	nsuccs = successors->nsuccs;

	for (succ = 0; succ < nsuccs; succ++)
	{
		struct _starpu_cg *cg = successors->succ[succ];
		STARPU_ASSERT(cg);

		struct _starpu_tag *cgtag = NULL;

		unsigned cg_type = cg->cg_type;

		if (cg_type == STARPU_CG_TAG)
		{
			cgtag = cg->succ.tag;
			STARPU_ASSERT(cgtag);
			_starpu_spin_lock(&cgtag->lock);
		}

		if (cg_type == STARPU_CG_TASK)
		{
			struct _starpu_job *j = cg->succ.job;
			_STARPU_PTHREAD_MUTEX_LOCK(&j->sync_mutex);
		}

		_starpu_notify_cg(cg);

		if (cg_type == STARPU_CG_TASK)
		{
			struct _starpu_job *j = cg->succ.job;

			/* In case this task was immediately terminated, since
			 * _starpu_notify_cg_list already hold the sync_mutex
			 * lock, it is its reponsability to destroy the task if
			 * needed. */
			unsigned must_destroy_task = 0;
			struct starpu_task *task = j->task;

			if (j->submitted && j->terminated > 0 && task->destroy && task->detach)
				must_destroy_task = 1;

			_STARPU_PTHREAD_MUTEX_UNLOCK(&j->sync_mutex);

			if (must_destroy_task)
				starpu_task_destroy(task);
		}

		if (cg_type == STARPU_CG_APPS)
		{
			/* Remove the temporary ref to the cg */
			memmove(&successors->succ[succ], &successors->succ[succ+1], (nsuccs-(succ+1)) * sizeof(successors->succ[succ]));
			succ--;
			nsuccs--;
			successors->nsuccs--;
		}

		if (cg_type == STARPU_CG_TAG)
			_starpu_spin_unlock(&cgtag->lock);
	}
}
