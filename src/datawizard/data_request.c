/*
 * StarPU
 * Copyright (C) INRIA 2008-2009 (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include <common/config.h>
#include <datawizard/data_request.h>
#include <pthread.h>

/* requests that have not been treated at all */
static data_request_list_t data_requests[MAXNODES];
static pthread_cond_t data_requests_list_cond[MAXNODES];
static pthread_mutex_t data_requests_list_mutex[MAXNODES];

/* requests that are not terminated (eg. async transfers) */
static data_request_list_t data_requests_pending[MAXNODES];
static pthread_cond_t data_requests_pending_list_cond[MAXNODES];
static pthread_mutex_t data_requests_pending_list_mutex[MAXNODES];

void init_data_request_lists(void)
{
	unsigned i;
	for (i = 0; i < MAXNODES; i++)
	{
		data_requests[i] = data_request_list_new();
		pthread_mutex_init(&data_requests_list_mutex[i], NULL);
		pthread_cond_init(&data_requests_list_cond[i], NULL);

		data_requests_pending[i] = data_request_list_new();
		pthread_mutex_init(&data_requests_pending_list_mutex[i], NULL);
		pthread_cond_init(&data_requests_pending_list_cond[i], NULL);
	}
}

void deinit_data_request_lists(void)
{
	unsigned i;
	for (i = 0; i < MAXNODES; i++)
	{
		pthread_cond_destroy(&data_requests_pending_list_cond[i]);
		pthread_mutex_destroy(&data_requests_pending_list_mutex[i]);
		data_request_list_delete(data_requests_pending[i]);

		pthread_cond_destroy(&data_requests_list_cond[i]);
		pthread_mutex_destroy(&data_requests_list_mutex[i]);
		data_request_list_delete(data_requests[i]);
	}
}

/* this should be called with the lock r->state->header_lock taken */
void data_request_destroy(data_request_t r)
{
	r->state->per_node[r->dst_node].request = NULL;

	data_request_delete(r);
}

/* state->lock should already be taken !  */
data_request_t create_data_request(data_state *state, uint32_t src_node, uint32_t dst_node, uint32_t handling_node, uint8_t read, uint8_t write, unsigned is_prefetch)
{
	data_request_t r = data_request_new();

	starpu_spin_init(&r->lock);

	r->state = state;
	r->src_node = src_node;
	r->dst_node = dst_node;
	r->read = read;
	r->write = write;

	r->handling_node = handling_node;

	r->completed = 0;
	r->retval = -1;

	r->next_req_count = 0;

	r->strictness = 1;
	r->is_a_prefetch_request = is_prefetch;

	/* associate that request with the state so that further similar
	 * requests will reuse that one  */

	starpu_spin_lock(&r->lock);

	state->per_node[dst_node].request = r;

	state->per_node[dst_node].refcnt++;
	state->per_node[src_node].refcnt++;

	r->refcnt = 1;

	//fprintf(stderr, "created request %p data %p %d -> %d \n", r, state, src_node, dst_node);

	starpu_spin_unlock(&r->lock);

	return r;
}

/* state->lock should be taken */
data_request_t search_existing_data_request(data_state *state, uint32_t dst_node, uint8_t read, uint8_t write)
{
	data_request_t r = state->per_node[dst_node].request;

	if (r)
	{
	//	/* XXX perhaps this is too strict ! */
	//	STARPU_ASSERT(r->read == read);		
	//	STARPU_ASSERT(r->write == write);		

		starpu_spin_lock(&r->lock);
	}

	return r;
}

int wait_data_request_completion(data_request_t r, unsigned may_alloc)
{
	int retval;
	int do_delete = 0;

	uint32_t local_node = get_local_memory_node();

	do {
		starpu_spin_lock(&r->lock);

		if (r->completed)
			break;

		starpu_spin_unlock(&r->lock);

		wake_all_blocked_workers_on_node(r->handling_node);

		datawizard_progress(local_node, may_alloc);

	} while (1);


	retval = r->retval;
	if (retval)
		fprintf(stderr, "REQUEST %p COMPLETED (retval %d) !\n", r, r->retval);
		

	r->refcnt--;

	/* if nobody is waiting on that request, we can get rid of it */
	if (r->refcnt == 0)
		do_delete = 1;

	starpu_spin_unlock(&r->lock);
	
	if (do_delete)
		data_request_destroy(r);
	
	return retval;
}

/* this is non blocking */
void post_data_request(data_request_t r, uint32_t handling_node)
{
	int res;
//	fprintf(stderr, "POST REQUEST\n");

	STARPU_ASSERT(r->state->per_node[r->src_node].allocated);
	STARPU_ASSERT(r->state->per_node[r->src_node].refcnt);

	/* insert the request in the proper list */
	res = pthread_mutex_lock(&data_requests_list_mutex[handling_node]);
	STARPU_ASSERT(!res);

	data_request_list_push_front(data_requests[handling_node], r);

	res = pthread_mutex_unlock(&data_requests_list_mutex[handling_node]);
	STARPU_ASSERT(!res);

	wake_all_blocked_workers_on_node(handling_node);
}

static void handle_data_request_completion(data_request_t r)
{
	unsigned do_delete = 0;
	data_state *state = r->state;

	update_data_state(state, r->dst_node, r->write);

#ifdef USE_FXT
	TRACE_END_DRIVER_COPY(r->src_node, r->dst_node, 0, r->com_id);
#endif

	/* TODO we should handle linked requests here */
	unsigned chained_req;
	for (chained_req = 0; chained_req < r->next_req_count; chained_req++)
	{
		post_data_request(r->next_req[chained_req], r->next_req[chained_req]->handling_node);
	}

	r->completed = 1;
	
	state->per_node[r->dst_node].refcnt--;
	state->per_node[r->src_node].refcnt--;

	r->refcnt--;

	/* if nobody is waiting on that request, we can get rid of it */
	if (r->refcnt == 0)
		do_delete = 1;
	
	r->retval = 0;

	starpu_spin_unlock(&r->lock);

	if (do_delete)
		data_request_destroy(r);

	starpu_spin_unlock(&state->header_lock);
}

/* TODO : accounting to see how much time was spent working for other people ... */
static int handle_data_request(data_request_t r, unsigned may_alloc)
{
	unsigned do_delete = 0;
	data_state *state = r->state;

//	fprintf(stderr, "handle_data_request %p %d->%d\n", r->state, r->src_node, r->dst_node);

	starpu_spin_lock(&state->header_lock);

	starpu_spin_lock(&r->lock);

	STARPU_ASSERT(state->per_node[r->src_node].allocated);
	STARPU_ASSERT(state->per_node[r->src_node].refcnt);

	/* perform the transfer */
	/* the header of the data must be locked by the worker that submitted the request */
	r->retval = driver_copy_data_1_to_1(state, r->src_node, r->dst_node, 0, r, may_alloc);

	if (r->retval == ENOMEM)
	{
		starpu_spin_unlock(&r->lock);
		starpu_spin_unlock(&state->header_lock);

		return ENOMEM;
	}

	if (r->retval == EAGAIN)
	{
		starpu_spin_unlock(&r->lock);
		starpu_spin_unlock(&state->header_lock);

		/* the request is pending and we put it in the corresponding queue  */
		pthread_mutex_lock(&data_requests_pending_list_mutex[r->handling_node]);
//		fprintf(stderr, "Push request %p (data %p) on pending list\n", r, r->state);
		data_request_list_push_front(data_requests_pending[r->handling_node], r);
		pthread_mutex_unlock(&data_requests_pending_list_mutex[r->handling_node]);

		return EAGAIN;
	}

	/* the request has been handled */
	handle_data_request_completion(r);

	return 0;
}

void handle_node_data_requests(uint32_t src_node, unsigned may_alloc)
{
	int res;

	/* for all entries of the list */
	data_request_t r;

	/* take all the entries from the request list */
	res = pthread_mutex_lock(&data_requests_list_mutex[src_node]);
	STARPU_ASSERT(!res);

	data_request_list_t local_list = data_requests[src_node];

	if (data_request_list_empty(local_list))
	{
		/* there is no request */
		res = pthread_mutex_unlock(&data_requests_list_mutex[src_node]);
		STARPU_ASSERT(!res);

		return;
	}

	data_requests[src_node] = data_request_list_new();

	res = pthread_mutex_unlock(&data_requests_list_mutex[src_node]);
	STARPU_ASSERT(!res);

	while (!data_request_list_empty(local_list))
	{
		r = data_request_list_pop_back(local_list);

		res = handle_data_request(r, may_alloc);
		if (res == ENOMEM)
		{
			res = pthread_mutex_lock(&data_requests_list_mutex[src_node]);
			STARPU_ASSERT(!res);

			data_request_list_push_front(data_requests[src_node], r);

			res = pthread_mutex_unlock(&data_requests_list_mutex[src_node]);
			STARPU_ASSERT(!res);
		}

		/* wake the requesting worker up */
		// if we do not progress ..
		// pthread_cond_broadcast(&data_requests_list_cond[src_node]);
	}

	data_request_list_delete(local_list);
}

static void _handle_pending_node_data_requests(uint32_t src_node, unsigned force)
{
	int res;
//	fprintf(stderr, "handle_pending_node_data_requests ...\n");

	res = pthread_mutex_lock(&data_requests_pending_list_mutex[src_node]);
	STARPU_ASSERT(!res);

	/* for all entries of the list */
	data_request_list_t local_list = data_requests_pending[src_node];
	data_requests_pending[src_node] = data_request_list_new();

	res = pthread_mutex_unlock(&data_requests_pending_list_mutex[src_node]);
	STARPU_ASSERT(!res);

	while (!data_request_list_empty(local_list))
	{
		data_request_t r;
		r = data_request_list_pop_back(local_list);

		data_state *state = r->state;
		
		starpu_spin_lock(&state->header_lock);
	
		starpu_spin_lock(&r->lock);
	
		/* wait until the transfer is terminated */
		if (force)
		{
			driver_wait_request_completion(&r->async_channel, src_node);
			handle_data_request_completion(r);
		}
		else {
			if (driver_test_request_completion(&r->async_channel, src_node))
			{
				
				handle_data_request_completion(r);
			}
			else {
				starpu_spin_unlock(&r->lock);
				starpu_spin_unlock(&state->header_lock);

				/* wake the requesting worker up */
				pthread_mutex_lock(&data_requests_pending_list_mutex[src_node]);
				data_request_list_push_front(data_requests_pending[src_node], r);
				pthread_mutex_unlock(&data_requests_pending_list_mutex[src_node]);
			}
		}
	}

	data_request_list_delete(local_list);
}

void handle_pending_node_data_requests(uint32_t src_node)
{
	_handle_pending_node_data_requests(src_node, 0);
}

void handle_all_pending_node_data_requests(uint32_t src_node)
{
	_handle_pending_node_data_requests(src_node, 1);
}


