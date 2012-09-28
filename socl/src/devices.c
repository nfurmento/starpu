/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2012 University of Bordeaux
 * Copyright (C) 2012 CNRS
 * Copyright (C) 2012 Vincent Danjean <Vincent.Danjean@ens-lyon.org>
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

#include "socl.h"
#include "devices.h"

// OpenCL 1.0 : Mandatory format: major_number.minor_number
const char * SOCL_DRIVER_VERSION = "0.1";

const cl_uint SOCL_DEVICE_VENDOR_ID = 666;

const struct _cl_device_id socl_virtual_device = {
   .dispatch = &socl_master_dispatch,
   .type = CL_DEVICE_TYPE_ACCELERATOR,
   .max_compute_units = 1,
   .max_work_item_dimensions = 3,
   .max_work_item_sizes = {1,1,1},
   .max_work_group_size = 1,
   .preferred_vector_widths = {16,8,4,2,4,2},
   .max_clock_frequency = 3000,
   .address_bits = 64,
   .max_mem_alloc_size = 1024*1024*1024,
   .image_support = CL_FALSE,
   .max_parameter_size = 256,
   .mem_base_addr_align = 0,
   .min_data_type_align_size = 0,
   .single_fp_config = CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN,
   .global_mem_cache_type = CL_READ_WRITE_CACHE,
   .global_mem_cacheline_size = 128,
   .global_mem_cache_size = 16*1024,
   .global_mem_size = (cl_ulong)4*1024*1024*1024,
   .max_constant_args = 8,
   .local_mem_type = CL_GLOBAL,
   .local_mem_size = 16*1024,
   .error_correction_support = CL_FALSE,
   .profiling_timer_resolution = 100,
   .endian_little = CL_TRUE,
   .available = CL_TRUE,
   .compiler_available = CL_TRUE,
   .execution_capabilities = CL_EXEC_KERNEL,
   .queue_properties = CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE,
   .name = "SOCL Virtual Device",
   .extensions = ""
};
