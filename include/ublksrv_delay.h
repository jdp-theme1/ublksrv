#ifndef UBLKSRV_DELAY_INC_H
#define UBLKSRV_DELAY_INC_H
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>


#include "ublk_cmd.h"
#include "ublksrv_utils.h"

extern int ublksrv_delay_module(int ublk_op);
extern int ublk_get_cpu_frequency();
extern int ublk_get_cpu_frequency_by_tick();

#endif

