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
// extern int ublksrv_delay_module_init(const struct ublksrv_ctrl_dev *dev);
extern int ublksrv_delay_module(const struct ublksrv_io_desc *iod);
extern void ublk_get_cpu_frequency();
extern int ublk_get_cpu_frequency_by_tick();

#endif

