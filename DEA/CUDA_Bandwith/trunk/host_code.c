#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <cuda.h>
#include <cuda_runtime_api.h>
#include "timing.h"
#include "param.h"

static CUcontext cuContext;
static CUmodule cuModule;
CUresult status;
extern char *execpath;

static CUfunction benchKernel = NULL;

unsigned offset = 0;
CUdeviceptr srcdevptr; 
CUdeviceptr dstdevptr; 
int *srchostptr;
int *dsthostptr;

#ifdef DEBUG
int host_debug = -1; 
CUdeviceptr dev_debug; 
#endif

unsigned errline = -1;

tick_t start, stop;


/*
 * parameters that can be tuned 
 */
unsigned datasize = DATASIZE;

int kernel = 2;

int griddimx = GRIDDIMX;
int griddimy = GRIDDIMY;
int blockdimx = BLOCKDIMX;
int blockdimy = BLOCKDIMY;

void compare(int *a, int *b, unsigned size)
{
	unsigned i;
	int diffcnt = 0;
	int firstbad = -1;
	int lastbad = -1;

	for (i = 0; i < size ; i++) 
	{
		if (a[i] != b[i]) 
		{
			diffcnt++;

			printf("a[%d] (%d) != b[%d] (%d)\n", i, a[i], i, b[i]);

			if (firstbad == -1) 
				firstbad = i;
			lastbad = i;
		}
	}

	if (diffcnt != 0) {
		printf("Matrix are DIFFERENT (%d diff out of %d, first bad %d, last bad = %d)\n", diffcnt, size, firstbad, lastbad);
	}
#ifdef DEBUG
	else {
		printf("Matrix are IDENTICAL\n");
	}
#endif

	return;
}	

void init_context()
{
	status = cuInit(0);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	status = cuCtxCreate( &cuContext, 0, 0);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	status = cuCtxAttach(&cuContext, 0);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}


	status = cuModuleLoad(&cuModule, "./kernel_code.cubin");
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	switch(kernel) {
		case 0: 
			status = cuModuleGetFunction( &benchKernel, cuModule, "bandwith_test_dumb");
			break;
		case 1:
			status = cuModuleGetFunction( &benchKernel, cuModule, "bandwith_test");
			break;
		case 2:
		default:
			status = cuModuleGetFunction( &benchKernel, cuModule, "bandwith_test_2");
			break;
	}

	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	return;

error:
	printf("oops  in %s line %d... %s \n", __func__, errline,cudaGetErrorString(status));
	assert(0);

}

void parse_args(int argc, char **argv)
{
	if (argc == 1) {
		/* use default arguments */
	}
	else {
		if (argc != 6) {
			fprintf(stderr, "Usage: %s kernel-id[0-2] griddimx griddimy blockdimx blockdimy\n", argv[0]);
			exit(-1);
		}
		else {
			/* parse the arguments */
			kernel = atoi(argv[1]);

			griddimx = atoi(argv[2]);
			griddimy = atoi(argv[3]);

			blockdimx = atoi(argv[4]);
			blockdimy = atoi(argv[5]);
		}
	}
}

int main(int argc, char **argv)
{
	parse_args(argc, argv);

	timing_init();

	init_context();

	/* allocate buffers on the device (slow memory) */
	status = cuMemAlloc(&srcdevptr, DATASIZE*sizeof(int));
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	status = cuMemAlloc(&dstdevptr, DATASIZE*sizeof(int));
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

#ifdef DEBUG
	status = cuMemAlloc(&dev_debug, sizeof(int));
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}
#endif

	srchostptr = (int *) malloc(DATASIZE*sizeof(int));
	dsthostptr = (int *) malloc(DATASIZE*sizeof(int));
 
	/* copy the data on the device */ 
	int i;
	for (i = 0; i < DATASIZE ; i++)
	{
		srchostptr[i] = 1;
		dsthostptr[i] = 1664;
	}

	status = cuMemcpyHtoD(srcdevptr, srchostptr, DATASIZE*sizeof(int));
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}
	
	/* launch the kernel */
	status = cuFuncSetBlockShape( benchKernel, blockdimx, blockdimy, 1);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

// 	status = cuFuncSetSharedSize(benchKernel, SHMEMSIZE);
//	if ( CUDA_SUCCESS != status )
//	{
//		errline = __LINE__;
//		goto error;
//	}

	/* stack the various parameters */
	status = cuParamSetv(benchKernel,offset,&srcdevptr,sizeof(CUdeviceptr));
	offset += sizeof(CUdeviceptr);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	status = cuParamSetv(benchKernel,offset,&dstdevptr,sizeof(CUdeviceptr));
	offset += sizeof(CUdeviceptr);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	status = cuParamSetv(benchKernel,offset, &datasize, sizeof(unsigned));
	offset += sizeof(unsigned);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

#ifdef DEBUG
	status = cuParamSetv(benchKernel,offset,&dev_debug,sizeof(int));
	offset += sizeof(int);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}
#endif

	status = cuParamSetSize(benchKernel, offset);
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}


	GET_TICK(start);

	status = cuLaunchGrid( benchKernel, griddimx, griddimy); 
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	/* wait for its termination */
	status = cuCtxSynchronize();
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}


	GET_TICK(stop);

	/* put data back into host memory for comparison */
	status = cuMemcpyDtoH(dsthostptr, dstdevptr, DATASIZE*sizeof(int));
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

#ifdef DEBUG
	status = cuMemcpyDtoH(&host_debug, dev_debug, sizeof(uint32_t));
	if ( CUDA_SUCCESS != status )
	{
		errline = __LINE__;
		goto error;
	}

	printf("DEBUG = %d \n", host_debug);
#endif

	compare(srchostptr, dsthostptr, DATASIZE);

	float chrono    =  (float)(TIMING_DELAY(start, stop));

	/* in B /us = MB/s */
	float bandwith = (float)((DATASIZE*2*sizeof(int))/(chrono));

#ifdef DEBUG
	printf("Computation time : %f ms\n", chrono/1000);
	printf("Bandwith %f MB/s\n", bandwith);
#else
	printf("%d\t%d\t%d\t%d\t%f\t%f\n", griddimx, griddimy, blockdimx, blockdimy, chrono/1000, bandwith);
#endif


	return 0;
error:
	printf("oops  in %s line %d... %s \n", __func__, errline,cudaGetErrorString(status));
	assert(0);
}
