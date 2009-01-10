#include <pthread.h>
#include <core/mechanisms/fifo_queues.h>
#include <errno.h>

/* keep track of the total number of jobs to be scheduled to avoid infinite 
 * polling when there are really few jobs in the overall queue */
static unsigned total_number_of_jobs;

static pthread_cond_t *sched_cond;
static pthread_mutex_t *sched_mutex;

void init_fifo_queues_mechanisms(void)
{
	total_number_of_jobs = 0;

	struct sched_policy_s *sched = get_sched_policy();

	/* to access them more easily, we keep their address in local variables */
	sched_cond = &sched->sched_activity_cond;
	sched_mutex = &sched->sched_activity_mutex;
}

struct jobq_s *create_fifo(void)
{
	struct jobq_s *jobq;
	jobq = malloc(sizeof(struct jobq_s));

	pthread_mutex_init(&jobq->activity_mutex, NULL);
	pthread_cond_init(&jobq->activity_cond, NULL);

	struct fifo_jobq_s *fifo;
	fifo = malloc(sizeof(struct fifo_jobq_s));

	/* note that not all mechanisms (eg. the semaphore) have to be used */
	fifo->jobq = job_list_new();
	fifo->njobs = 0;
	fifo->nprocessed = 0;

	fifo->exp_start = timing_now()/1000000;
	fifo->exp_len = 0.0;
	fifo->exp_end = fifo->exp_start;

	jobq->queue = fifo;

	return jobq;
}

unsigned get_total_njobs_fifos(void)
{
	return total_number_of_jobs;
}

unsigned get_fifo_njobs(struct jobq_s *q)
{
	STARPU_ASSERT(q);

	struct fifo_jobq_s *fifo_queue = q->queue;

	return fifo_queue->njobs;
}

unsigned get_fifo_nprocessed(struct jobq_s *q)
{
	STARPU_ASSERT(q);

	struct fifo_jobq_s *fifo_queue = q->queue;

	return fifo_queue->nprocessed;
}

int fifo_push_prio_task(struct jobq_s *q, job_t task)
{
#ifndef NO_PRIO
	STARPU_ASSERT(q);
	struct fifo_jobq_s *fifo_queue = q->queue;

	/* if anyone is blocked on the entire machine, wake it up */
	pthread_mutex_lock(sched_mutex);
	total_number_of_jobs++;
	pthread_cond_signal(sched_cond);
	pthread_mutex_unlock(sched_mutex);
	
	/* wake people waiting locally */
	pthread_mutex_lock(&q->activity_mutex);

	TRACE_JOB_PUSH(task, 0);
	job_list_push_back(fifo_queue->jobq, task);
	fifo_queue->njobs++;
	fifo_queue->nprocessed++;

	pthread_cond_signal(&q->activity_cond);
	pthread_mutex_unlock(&q->activity_mutex);

	return 0;
#else
	return fifo_push_task(q, task);
#endif
}

int fifo_push_task(struct jobq_s *q, job_t task)
{
	STARPU_ASSERT(q);
	struct fifo_jobq_s *fifo_queue = q->queue;

	/* if anyone is blocked on the entire machine, wake it up */
	pthread_mutex_lock(sched_mutex);
	total_number_of_jobs++;
	pthread_cond_signal(sched_cond);
	pthread_mutex_unlock(sched_mutex);
	
	/* wake people waiting locally */
	pthread_mutex_lock(&q->activity_mutex);

	TRACE_JOB_PUSH(task, 0);
	job_list_push_front(fifo_queue->jobq, task);
	fifo_queue->njobs++;
	fifo_queue->nprocessed++;

	pthread_cond_signal(&q->activity_cond);
	pthread_mutex_unlock(&q->activity_mutex);

	return 0;
}

job_t fifo_pop_task(struct jobq_s *q)
{
	job_t j = NULL;

	STARPU_ASSERT(q);
	struct fifo_jobq_s *fifo_queue = q->queue;

	/* block until some event happens */
	pthread_mutex_lock(&q->activity_mutex);

	if (fifo_queue->njobs == 0)
		pthread_cond_wait(&q->activity_cond, &q->activity_mutex);

	if (fifo_queue->njobs > 0) 
	{
		/* there is a task */
		j = job_list_pop_back(fifo_queue->jobq);
	
		STARPU_ASSERT(j);
		fifo_queue->njobs--;
		
		TRACE_JOB_POP(j, 0);

		/* we are sure that we got it now, so at worst, some people thought 
		 * there remained some work and will soon discover it is not true */
		pthread_mutex_lock(sched_mutex);
		total_number_of_jobs--;
		pthread_mutex_unlock(sched_mutex);
	}
	
	pthread_mutex_unlock(&q->activity_mutex);

	return j;
}

/* for work stealing, typically */
job_t fifo_non_blocking_pop_task(struct jobq_s *q)
{
	job_t j = NULL;

	STARPU_ASSERT(q);
	struct fifo_jobq_s *fifo_queue = q->queue;

	/* block until some event happens */
	pthread_mutex_lock(&q->activity_mutex);

	if (fifo_queue->njobs > 0) 
	{
		/* there is a task */
		j = job_list_pop_back(fifo_queue->jobq);
	
		STARPU_ASSERT(j);
		fifo_queue->njobs--;
		
		TRACE_JOB_POP(j, 0);

		/* we are sure that we got it now, so at worst, some people thought 
		 * there remained some work and will soon discover it is not true */
		pthread_mutex_lock(sched_mutex);
		total_number_of_jobs--;
		pthread_mutex_unlock(sched_mutex);
	}
	
	pthread_mutex_unlock(&q->activity_mutex);

	return j;
}

job_t fifo_non_blocking_pop_task_if_job_exists(struct jobq_s *q)
{
	job_t j;

	j = fifo_non_blocking_pop_task(q);

	if (!j) {
		/* there is no job at all in the entire system : go to sleep ! */

		/* that wait is not an absolute sign that there is some work 
		 * if there is some, the thread should be awoken, but if there is none 
		 * at the moment it is awoken, it may simply poll a limited number of 
		 * times and just get back to sleep */
		pthread_mutex_lock(sched_mutex);

		if (total_number_of_jobs == 0)
			pthread_cond_wait(sched_cond, sched_mutex);

		pthread_mutex_unlock(sched_mutex);
	}

	return j;
}
