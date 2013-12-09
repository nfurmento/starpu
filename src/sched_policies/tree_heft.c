/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2013  Université de Bordeaux 1
 * Copyright (C) 2013  INRIA
 * Copyright (C) 2013  Simon Archipoff
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

#include <starpu_sched_component.h>
#include <starpu_scheduler.h>
#include <float.h>

/* The two thresolds concerns the prio components, which contains queues
 * who can handle the priority of StarPU tasks. You can tune your
 * scheduling by benching those values and choose which one is the
 * best for your current application. 
 * The current value of the ntasks_threshold is the best we found
 * so far across several types of applications (cholesky, LU, stencil).
 */
#define _STARPU_SCHED_NTASKS_THRESHOLD_DEFAULT 30
#define _STARPU_SCHED_EXP_LEN_THRESHOLD_DEFAULT 1000000000.0

static void initialize_heft_center_policy(unsigned sched_ctx_id)
{
	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);

	unsigned ntasks_threshold = _STARPU_SCHED_NTASKS_THRESHOLD_DEFAULT;
	double exp_len_threshold = _STARPU_SCHED_EXP_LEN_THRESHOLD_DEFAULT;

	const char *strval_ntasks_threshold = getenv("STARPU_NTASKS_THRESHOLD");
	if (strval_ntasks_threshold)
		ntasks_threshold = atof(strval_ntasks_threshold);

	const char *strval_exp_len_threshold = getenv("STARPU_EXP_LEN_THRESHOLD");
	if (strval_exp_len_threshold)
		exp_len_threshold = atof(strval_exp_len_threshold);


/* The scheduling strategy look like this :
 *
 *                                    |
 *                              window_component
 *                                    |
 * perfmodel_component <--push-- perfmodel_select_component --push--> eager_component
 *          |                                                    |
 *          |                                                    |
 *          >----------------------------------------------------<
 *                    |                                |
 *              best_impl_component                    best_impl_component
 *                    |                                |
 *                prio_component                        prio_component
 *                    |                                |
 *               worker_component                   worker_component
 *
 * A window contain the tasks that failed to be pushed, so as when the prio_components reclaim
 * tasks by calling can_push to their father (classically, just after a successful pop have
 * been made by its associated worker_component), this call goes up to the window_component which
 * pops a task from its local queue and try to schedule it by pushing it to the
 * decision_component. 
 * Finally, the task will be pushed to the prio_component which is the direct
 * father in the tree of the worker_component the task has been scheduled on. This
 * component will push the task on its local queue if no one of the two thresholds
 * have been reached for it, or send a push_error signal to its father.
 */
	struct starpu_sched_tree * t = starpu_sched_tree_create(sched_ctx_id);

	struct starpu_sched_component * window_component = starpu_sched_component_prio_create(NULL);
	t->root = window_component;

	struct starpu_sched_component * perfmodel_component = starpu_sched_component_mct_create(NULL);
	struct starpu_sched_component * no_perfmodel_component = starpu_sched_component_eager_create(NULL);
	struct starpu_sched_component * calibrator_component = starpu_sched_component_eager_calibration_create(NULL);
	
	struct starpu_perfmodel_select_data perfmodel_select_data =
		{
			.calibrator_component = calibrator_component,
			.no_perfmodel_component = no_perfmodel_component,
			.perfmodel_component = perfmodel_component,
		};

	struct starpu_sched_component * perfmodel_select_component = starpu_sched_component_perfmodel_select_create(&perfmodel_select_data);
	window_component->add_child(window_component, perfmodel_select_component);
	perfmodel_select_component->add_father(perfmodel_select_component, window_component);

	perfmodel_select_component->add_child(perfmodel_select_component, calibrator_component);
	calibrator_component->add_father(calibrator_component, perfmodel_select_component);
	perfmodel_select_component->add_child(perfmodel_select_component, perfmodel_component);
	perfmodel_component->add_father(perfmodel_component, perfmodel_select_component);
	perfmodel_select_component->add_child(perfmodel_select_component, no_perfmodel_component);
	no_perfmodel_component->add_father(no_perfmodel_component, perfmodel_select_component);

	struct starpu_prio_data prio_data =
		{
			.ntasks_threshold = ntasks_threshold,
			.exp_len_threshold = exp_len_threshold,
		};

	unsigned i;
	for(i = 0; i < starpu_worker_get_count() + starpu_combined_worker_get_count(); i++)
	{
		struct starpu_sched_component * worker_component = starpu_sched_component_worker_get(i);
		STARPU_ASSERT(worker_component);

		struct starpu_sched_component * prio_component = starpu_sched_component_prio_create(&prio_data);
		prio_component->add_child(prio_component, worker_component);
		worker_component->add_father(worker_component, prio_component);

		struct starpu_sched_component * impl_component = starpu_sched_component_best_implementation_create(NULL);
		impl_component->add_child(impl_component, prio_component);
		prio_component->add_father(prio_component, impl_component);

		perfmodel_component->add_child(perfmodel_component, impl_component);
		impl_component->add_father(impl_component, perfmodel_component);
		no_perfmodel_component->add_child(no_perfmodel_component, impl_component);
		impl_component->add_father(impl_component, no_perfmodel_component);
		calibrator_component->add_child(calibrator_component, impl_component);
		impl_component->add_father(impl_component, calibrator_component);
	}

	starpu_sched_tree_update_workers(t);
	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)t);
}

static void deinitialize_heft_center_policy(unsigned sched_ctx_id)
{
	struct starpu_sched_tree *t = (struct starpu_sched_tree*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	starpu_sched_tree_destroy(t);
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);
}

struct starpu_sched_policy _starpu_sched_tree_heft_policy =
{
	.init_sched = initialize_heft_center_policy,
	.deinit_sched = deinitialize_heft_center_policy,
	.add_workers = starpu_sched_tree_add_workers,
	.remove_workers = starpu_sched_tree_remove_workers,
	.push_task = starpu_sched_tree_push_task,
	.pop_task = starpu_sched_tree_pop_task,
	.pre_exec_hook = starpu_sched_component_worker_pre_exec_hook,
	.post_exec_hook = starpu_sched_component_worker_post_exec_hook,
	.pop_every_task = NULL,
	.policy_name = "tree-heft",
	.policy_description = "heft tree policy"
};
