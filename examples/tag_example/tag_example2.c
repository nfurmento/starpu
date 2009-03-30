#include <semaphore.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

#include <starpu.h>

#define TAG(i, iter)	((uint64_t)  (((uint64_t)iter)<<32 | (i)) )

sem_t sem;
starpu_codelet cl;

#define Ni	64
#define Nk	2

static unsigned ni = Ni, nk = Nk;
static unsigned callback_cnt;
static unsigned iter = 0;

static void parse_args(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-iter") == 0) {
		        char *argptr;
			nk = strtol(argv[++i], &argptr, 10);
		}

		if (strcmp(argv[i], "-i") == 0) {
		        char *argptr;
			ni = strtol(argv[++i], &argptr, 10);
		}

		if (strcmp(argv[i], "-h") == 0) {
			printf("usage : %s [-iter iter] [-i i]\n", argv[0]);
		}
	}
}

void callback_core(void *argcb);

static void tag_cleanup_grid(unsigned ni, unsigned iter)
{
	unsigned i;

	for (i = 0; i < ni; i++)
	{
		starpu_tag_remove(TAG(i,iter));
	}


} 

static void create_task_grid(unsigned iter)
{
	unsigned i;

	fprintf(stderr, "start iter %d ni %d...\n", iter, ni);

	callback_cnt = (ni);

	for (i = 0; i < ni; i++)
	{
		/* create a new task */
		struct starpu_task *task = starpu_task_create();
		task->callback_func = callback_core;
		//jb->argcb = &coords[i][j];
		task->cl = &cl;
		task->cl_arg = NULL;

		task->use_tag = 1;
		task->tag_id = TAG(i, iter);

		if (i != 0)
			starpu_tag_declare_deps(TAG(i,iter), 1, TAG(i-1,iter));

		starpu_submit_task(task);
	}
}


void callback_core(void *argcb __attribute__ ((unused)))
{
	unsigned newcnt = STARPU_ATOMIC_ADD(&callback_cnt, -1);	

	if (newcnt == 0)
	{
		
		iter++;
		if (iter < nk)
		{
			/* cleanup old grids ... */
			if (iter > 2)
				tag_cleanup_grid(ni, iter-2);

			/* create a new iteration */
			create_task_grid(iter);
		}
		else {
			sem_post(&sem);
		}
	}
}

void core_codelet(void *_args __attribute__ ((unused)))
{
}

int main(int argc __attribute__((unused)) , char **argv __attribute__((unused)))
{
	starpu_init();

	parse_args(argc, argv);

	cl.core_func = core_codelet;
	cl.cublas_func = core_codelet;
	cl.where = CORE;
	cl.nbuffers = 0;

	sem_init(&sem, 0, 0);

	create_task_grid(0);

	sem_wait(&sem);

	starpu_shutdown();

	fprintf(stderr, "TEST DONE ...\n");

	return 0;
}
