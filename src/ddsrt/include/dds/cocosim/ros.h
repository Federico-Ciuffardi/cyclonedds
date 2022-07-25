#ifndef COCOSIM_ROS_H
#define COCOSIM_ROS_H

#include <string.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "dds/cocosim/log.h"
#include <stdatomic.h>
#include <pthread.h>

#define COCOSIM_NS_PID_FILE_PATH "/tmp/cocosim_ns_pid"

/* Get the process ID of the calling process's nth ancestor.  */
pid_t getapid(int n); 

/* Get the ros namespace associated with the process ID of the calling process */
/* ns is READ ONLY */
void get_ns(char** ns);


/* True if CoCoSim is enabled and false othrewise */
void get_ccs_enabled(bool* ccs_enabled);

#endif /* COCOSIM_ROS_H */
