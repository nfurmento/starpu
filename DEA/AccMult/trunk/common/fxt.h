#ifndef __FXT_H__
#define __FXT_H__


#define _GNU_SOURCE  /* ou _BSD_SOURCE ou _SVID_SOURCE */
#include <unistd.h>
#include <sys/syscall.h> /* pour les définitions de SYS_xxx */

#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <common/util.h>

/* some key to identify the worker kind */
#define FUT_APPS_KEY	0x100
#define FUT_CORE_KEY	0x101
#define FUT_CUDA_KEY	0x102
#define FUT_CUBLAS_KEY	0x103

#define	FUT_NEW_WORKER_KEY	0x5102
#define	FUT_START_CODELET_BODY	0x5103
#define	FUT_END_CODELET_BODY	0x5104

#define FUT_JOB_PUSH		0x5105
#define FUT_JOB_POP		0x5106

#ifdef USE_FXT
#include <fxt/fxt.h>
#include <fxt/fut.h>

void start_fxt_profiling(void);
void fxt_register_thread(unsigned);

/* workerkind = FUT_CORE_KEY for instance */
#define TRACE_NEW_WORKER(workerkind)	\
	FUT_DO_PROBE2(FUT_NEW_WORKER_KEY, workerkind, syscall(SYS_gettid));

#define TRACE_START_CODELET_BODY(job)	\
	FUT_DO_PROBE2(FUT_START_CODELET_BODY, job, syscall(SYS_gettid));

#define TRACE_END_CODELET_BODY(job)	\
	FUT_DO_PROBE2(FUT_END_CODELET_BODY, job, syscall(SYS_gettid));

#define TRACE_JOB_PUSH(task, prio)	\
	FUT_DO_PROBE3(FUT_JOB_PUSH, task, prio, syscall(SYS_gettid));

#define TRACE_JOB_POP(task, prio)	\
	FUT_DO_PROBE3(FUT_JOB_POP, task, prio, syscall(SYS_gettid));


#else // !USE_FXT

#define TRACE_NEW_WORKER(a)		do {} while(0);
#define TRACE_START_CODELET_BODY(job)	do {} while(0);
#define TRACE_END_CODELET_BODY(job)	do {} while(0);
#define TRACE_JOB_PUSH(task, prio)	do {} while(0);
#define TRACE_JOB_POP(task, prio)	do {} while(0);

#endif // USE_FXT

#endif // __FXT_H__
