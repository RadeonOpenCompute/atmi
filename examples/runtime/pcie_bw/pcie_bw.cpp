/*===--------------------------------------------------------------------------
 *              ATMI (Asynchronous Task and Memory Interface)
 *
 * This file is distributed under the MIT License. See LICENSE.txt for details.
 *===------------------------------------------------------------------------*/

#include "atmi_runtime.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
using namespace std;
#ifdef __cplusplus
#define _CPPSTRING_ "C"
#endif
#ifndef __cplusplus
#define _CPPSTRING_
#endif

#define ErrorCheck(status) \
if (status != ATMI_STATUS_SUCCESS) { \
    printf("Error at [%s:%d]\n", __FILE__, __LINE__); \
    exit(1); \
}
#define NSECPERSEC 1000000000L
#define NTIMERS 13
long int get_nanosecs( struct timespec start_time, struct timespec end_time) {
  long int nanosecs;
  if ((end_time.tv_nsec-start_time.tv_nsec)<0) nanosecs =
    ((((long int) end_time.tv_sec- (long int) start_time.tv_sec )-1)*NSECPERSEC ) +
      ( NSECPERSEC + (long int) end_time.tv_nsec - (long int) start_time.tv_nsec) ;
  else nanosecs =
    (((long int) end_time.tv_sec- (long int) start_time.tv_sec )*NSECPERSEC ) +
      ( (long int) end_time.tv_nsec - (long int) start_time.tv_nsec );
  return nanosecs;
}

int main(int argc, char **argv) {
  ErrorCheck(atmi_init(ATMI_DEVTYPE_ALL));

  int gpu_id = 0;
  int cpu_id = 0;
  atmi_machine_t *machine = atmi_machine_get_info();
  int gpu_count = machine->device_count_by_type[ATMI_DEVTYPE_GPU];
  if(argv[1] != NULL) gpu_id = (atoi(argv[1]) % gpu_count);
  printf("Choosing GPU %d/%d\n", gpu_id, gpu_count);

  struct timespec start_time[NTIMERS],end_time[NTIMERS];
  long int kcalls, nanosecs[NTIMERS];
  float bw[NTIMERS];
  kcalls = 100;

  /* Run HelloWorld on GPU */
  atmi_mem_place_t gpu = ATMI_MEM_PLACE(ATMI_DEVTYPE_GPU, gpu_id, 0);
  atmi_mem_place_t cpu = ATMI_MEM_PLACE(ATMI_DEVTYPE_CPU, cpu_id, 0);
  void *d_input, *d_output;
  atmi_taskgroup_handle_t group;
  ErrorCheck(atmi_taskgroup_create(&group));

  ATMI_CPARM(cparm);
  cparm->groupable = ATMI_TRUE;
  cparm->group = group;

  printf("Size (MB)\t");
#ifdef BIBW
  printf("Bi-dir BW(MBps)\n");
#else
  printf("H2D BW(MBps)\tD2H BW(MBps)\n");
#endif
  const long MB = 1024 * 1024;
  for(long size = 1*MB; size <= 1024*MB; size *= 2) {
    clock_gettime(CLOCK_MONOTONIC_RAW,&start_time[0]);
    ErrorCheck(atmi_malloc(&d_input, size, cpu));
    ErrorCheck(atmi_malloc(&d_output, size, gpu));
    /* touch */
    memset(d_input, 0, size);
    ErrorCheck(atmi_memcpy(d_output, d_input, size));
    clock_gettime(CLOCK_MONOTONIC_RAW,&end_time[0]);

    clock_gettime(CLOCK_MONOTONIC_RAW,&start_time[1]);
    clock_gettime(CLOCK_MONOTONIC_RAW,&start_time[2]);
    for(int i=0; i<kcalls; i++) {
      atmi_memcpy_async(cparm, d_output, d_input, size);
    }
    // wait for all tasks to complete
#ifndef BIBW
    ErrorCheck(atmi_taskgroup_wait(group));
#endif
    clock_gettime(CLOCK_MONOTONIC_RAW,&end_time[2]);

    clock_gettime(CLOCK_MONOTONIC_RAW,&start_time[3]);
    for(int i=0; i<kcalls; i++) {
      atmi_memcpy_async(cparm, d_input, d_output, size);
    }
    // wait for all tasks to complete
    ErrorCheck(atmi_taskgroup_wait(group));
    clock_gettime(CLOCK_MONOTONIC_RAW,&end_time[3]);
    clock_gettime(CLOCK_MONOTONIC_RAW,&end_time[1]);

    for(int i=0; i<NTIMERS; i++) {
      nanosecs[i] = get_nanosecs(start_time[i],end_time[i]);
      bw[i] = ((float) kcalls * (size/MB) * (float) NSECPERSEC) / (float) nanosecs[i];
    }

    printf("%lu\t\t", size/MB);
#ifdef BIBW
    printf("%.0f\n",2*bw[1]);
#else
    printf("%.0f\t\t",bw[2]);
    printf("%.0f\n",bw[3]);
#endif

    /* cleanup */
    ErrorCheck(atmi_free(d_input));
    ErrorCheck(atmi_free(d_output));
  }

  ErrorCheck(atmi_finalize());
  return 0;
}
