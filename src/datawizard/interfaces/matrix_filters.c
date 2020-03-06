/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2013  Université de Bordeaux
 * Copyright (C) 2010  Mehdi Juhoor
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

#include <starpu.h>
#include <common/config.h>
#include <datawizard/filters.h>

/*
 * an example of a dummy partition function : blocks ...
 */
void starpu_matrix_filter_block(void *father_interface, void *child_interface, STARPU_ATTRIBUTE_UNUSED struct starpu_data_filter *f, unsigned id, unsigned nchunks)
{
	struct starpu_matrix_interface *matrix_father = (struct starpu_matrix_interface *) father_interface;
	struct starpu_matrix_interface *matrix_child = (struct starpu_matrix_interface *) child_interface;

	uint32_t nx = matrix_father->nx;
	uint32_t ny = matrix_father->ny;
	size_t elemsize = matrix_father->elemsize;

	STARPU_ASSERT_MSG(nchunks <= nx, "%u parts for %u elements", nchunks, nx);

	uint32_t child_nx;
	size_t offset;

	_starpu_filter_nparts_compute_chunk_size_and_offset(nx, nchunks, elemsize, id, 1,
						     &child_nx, &offset);

	/* update the child's interface */
	matrix_child->nx = child_nx;
	matrix_child->ny = ny;
	matrix_child->elemsize = elemsize;

	/* is the information on this node valid ? */
	if (matrix_father->dev_handle)
	{
		if (matrix_father->ptr)
			matrix_child->ptr = matrix_father->ptr + offset;
		matrix_child->ld = matrix_father->ld;
		matrix_child->dev_handle = matrix_father->dev_handle;
		matrix_child->offset = matrix_father->offset + offset;
	}
}

/*
 * an example of a dummy partition function : blocks ...
 */
void starpu_matrix_filter_block_shadow(void *father_interface, void *child_interface, STARPU_ATTRIBUTE_UNUSED struct starpu_data_filter *f, unsigned id, unsigned nchunks)
{
	struct starpu_matrix_interface *matrix_father = (struct starpu_matrix_interface *) father_interface;
	struct starpu_matrix_interface *matrix_child = (struct starpu_matrix_interface *) child_interface;

	uintptr_t shadow_size = (uintptr_t) f->filter_arg_ptr;

	/* actual number of elements */
	uint32_t nx = matrix_father->nx - 2 * shadow_size;
	uint32_t ny = matrix_father->ny;
	size_t elemsize = matrix_father->elemsize;

	STARPU_ASSERT_MSG(nchunks <= nx, "%u parts for %u elements", nchunks, nx);

	uint32_t child_nx;
	size_t offset;

	_starpu_filter_nparts_compute_chunk_size_and_offset(nx, nchunks, elemsize, id, 1,
						     &child_nx, &offset);

	child_nx += 2 * shadow_size;

	/* update the child's interface */
	matrix_child->nx = child_nx;
	matrix_child->ny = ny;
	matrix_child->elemsize = elemsize;

	/* is the information on this node valid ? */
	if (matrix_father->dev_handle)
	{
		if (matrix_father->ptr)
			matrix_child->ptr = matrix_father->ptr + offset;
		matrix_child->ld = matrix_father->ld;
		matrix_child->dev_handle = matrix_father->dev_handle;
		matrix_child->offset = matrix_father->offset + offset;
	}
}

void starpu_matrix_filter_vertical_block(void *father_interface, void *child_interface, STARPU_ATTRIBUTE_UNUSED struct starpu_data_filter *f, unsigned id, unsigned nchunks)
{
        struct starpu_matrix_interface *matrix_father = (struct starpu_matrix_interface *) father_interface;
        struct starpu_matrix_interface *matrix_child = (struct starpu_matrix_interface *) child_interface;

	uint32_t nx = matrix_father->nx;
	uint32_t ny = matrix_father->ny;
	size_t elemsize = matrix_father->elemsize;

	STARPU_ASSERT_MSG(nchunks <= ny, "%u parts for %u elements", nchunks, ny);

	uint32_t child_ny;
	size_t offset;

	_starpu_filter_nparts_compute_chunk_size_and_offset(ny, nchunks, elemsize, id,
						     matrix_father->ld,
						     &child_ny, &offset);

	matrix_child->nx = nx;
	matrix_child->ny = child_ny;
	matrix_child->elemsize = elemsize;

	/* is the information on this node valid ? */
	if (matrix_father->dev_handle)
	{
		if (matrix_father->ptr)
			matrix_child->ptr = matrix_father->ptr + offset;
		matrix_child->ld = matrix_father->ld;
		matrix_child->dev_handle = matrix_father->dev_handle;
		matrix_child->offset = matrix_father->offset + offset;
	}
}

void starpu_matrix_filter_vertical_block_shadow(void *father_interface, void *child_interface, STARPU_ATTRIBUTE_UNUSED struct starpu_data_filter *f, unsigned id, unsigned nchunks)
{
        struct starpu_matrix_interface *matrix_father = (struct starpu_matrix_interface *) father_interface;
        struct starpu_matrix_interface *matrix_child = (struct starpu_matrix_interface *) child_interface;

	uintptr_t shadow_size = (uintptr_t) f->filter_arg_ptr;

	uint32_t nx = matrix_father->nx;
	/* actual number of elements */
	uint32_t ny = matrix_father->ny - 2 * shadow_size;
	size_t elemsize = matrix_father->elemsize;

	STARPU_ASSERT_MSG(nchunks <= ny, "%u parts for %u elements", nchunks, ny);

	uint32_t child_ny;
	size_t offset;

	_starpu_filter_nparts_compute_chunk_size_and_offset(ny, nchunks, elemsize, id,
						     matrix_father->ld,
						     &child_ny, &offset);
	child_ny += 2 * shadow_size;

	matrix_child->nx = nx;
	matrix_child->ny = child_ny;
	matrix_child->elemsize = elemsize;

	/* is the information on this node valid ? */
	if (matrix_father->dev_handle)
	{
		if (matrix_father->ptr)
			matrix_child->ptr = matrix_father->ptr + offset;
		matrix_child->ld = matrix_father->ld;
		matrix_child->dev_handle = matrix_father->dev_handle;
		matrix_child->offset = matrix_father->offset + offset;
	}
}
