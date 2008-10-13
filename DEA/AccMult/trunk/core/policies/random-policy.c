#include <core/policies/random-policy.h>

/* XXX 32 is set randomly */
unsigned nworkers;
struct jobq_s *queue_array[32];

static job_t random_pop_task(struct jobq_s *q)
{
	struct job_s *j;

	j = fifo_pop_task(q);

	return j;
}

static void _random_push_task(struct jobq_s *q __attribute__ ((unused)), job_t task, unsigned prio)
{
	/* find the queue */
	struct fifo_jobq_s *fifo;
	unsigned worker;

	unsigned selected = 0;

	double alpha_sum = 0.0;

	for (worker = 0; worker < nworkers; worker++)
	{
		alpha_sum += queue_array[worker]->alpha;
	}

	double rand = drand48()*alpha_sum;
//	fprintf(stderr, "my rand is %e\n", rand);

	double alpha = 0.0;
	for (worker = 0; worker < nworkers; worker++)
	{
		if (alpha + queue_array[worker]->alpha > rand) {
			/* we found the worker */
			selected = worker;
			break;
		}

		alpha += queue_array[worker]->alpha;
	}

	/* we should now have the best worker in variable "best" */
	fifo = queue_array[selected]->queue;

	if (prio) {
		fifo_push_prio_task(queue_array[selected], task);
	} else {
		fifo_push_task(queue_array[selected], task);
	}
}

static void random_push_prio_task(struct jobq_s *q, job_t task)
{
	_random_push_task(q, task, 1);
}

static void random_push_task(struct jobq_s *q, job_t task)
{
	_random_push_task(q, task, 0);
}

static struct jobq_s *init_random_fifo(void)
{
	struct jobq_s *q;

	q = create_fifo();

	q->push_task = random_push_task; 
	q->push_prio_task = random_push_prio_task; 
	q->pop_task = random_pop_task;
	q->who = 0;

	queue_array[nworkers++] = q;

	return q;
}

void initialize_random_policy(struct machine_config_s *config, 
 __attribute__ ((unused)) struct sched_policy_s *_policy) 
{
	nworkers = 0;

	srand48(time(NULL));

	setup_queues(init_fifo_queues_mechanisms, init_random_fifo, config);
}

struct jobq_s *get_local_queue_random(struct sched_policy_s *policy __attribute__ ((unused)))
{
	struct jobq_s *queue;
	queue = pthread_getspecific(policy->local_queue_key);

	if (!queue)
	{
		/* take one randomly as this *must* be for a push anyway XXX */
		queue = queue_array[0];
	}

	return queue;
}

