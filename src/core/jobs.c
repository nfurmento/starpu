#include <core/jobs.h>
#include <core/workers.h>
#include <core/dependencies/data-concurrency.h>

size_t job_get_data_size(job_t j)
{
	size_t size = 0;

	struct starpu_task *task = j->task;

	unsigned nbuffers = task->cl->nbuffers;

	unsigned buffer;
	for (buffer = 0; buffer < nbuffers; buffer++)
	{
		data_state *state = task->buffers[buffer].state;
		size += state->ops->get_size(state);
	}

	return size;
}

/* create an internal job_t structure to encapsulate the task */
job_t job_create(struct starpu_task *task)
{
	job_t job;

	job = job_new();

	job->task = task;

	job->predicted = 0.0;
	job->footprint_is_computed = 0;
	job->terminated = 0;

	if (task->synchronous)
		sem_init(&job->sync_sem, 0, 0);

	if (task->use_tag)
		tag_declare(task->tag_id, job);

	return job;
}

struct starpu_task *starpu_task_create(void)
{
	struct starpu_task *task;

	task = calloc(1, sizeof(struct starpu_task));
	STARPU_ASSERT(task);

	task->priority = DEFAULT_PRIO;

	return task;
}

void handle_job_termination(job_t j)
{
	struct starpu_task *task = j->task;

	if (STARPU_UNLIKELY(j->terminated))
		fprintf(stderr, "OOPS ... job %p was already terminated !!\n", j);

	j->terminated = 1;

	/* in case there are dependencies, wake up the proper tasks */
	notify_dependencies(j);

	/* the callback is executed after the dependencies so that we may remove the tag 
 	 * of the task itself */
	if (task->callback_func)
	{
		TRACE_START_CALLBACK(j);
		task->callback_func(task->callback_arg);
		TRACE_END_CALLBACK(j);
	}

	if (task->synchronous)
		sem_post(&j->sync_sem);
}

static void block_if_sync_task(job_t j)
{
	if (j->task->synchronous)
	{
		sem_wait(&j->sync_sem);
		sem_destroy(&j->sync_sem);
	}
}

/* application should submit new tasks to StarPU through this function */
int starpu_submit_task(struct starpu_task *task)
{
	int ret;
	STARPU_ASSERT(task);

	if (!worker_exists(task->cl->where))
		return -ENODEV;

	/* internally, StarPU manipulates a job_t which is a wrapper around a
 	* task structure */
	job_t j = job_create(task);

	/* enfore task dependencies */
	if (task->use_tag)
	{
		if (submit_job_enforce_task_deps(j))
		{
			block_if_sync_task(j);
			return 0;
		}
	}

#ifdef NO_DATA_RW_LOCK
	/* enforce data dependencies */
	if (submit_job_enforce_data_deps(j))
	{
		block_if_sync_task(j);
		return 0;
	}
#endif

	ret = push_task(j);

	block_if_sync_task(j);

	return ret;
}

//int submit_prio_job(job_t j)
//{
//	j->priority = MAX_PRIO;
//	
//	return submit_job(j);
//}

/* This function is supplied for convenience only, it is equivalent to setting
 * the proper flag and submitting the task with submit_task.
 * Note that this call is blocking, and will not make StarPU progress,
 * so it must only be called from the programmer thread, not by StarPU.
 * NB: This also means that it cannot be submitted within a callback ! */
int submit_sync_task(struct starpu_task *task)
{
	task->synchronous = 1;

	return starpu_submit_task(task);
}
