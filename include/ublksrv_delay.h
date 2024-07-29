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

struct ublksrv_delay
{
	/* device information*/
	uint16_t choas_index;
	uint16_t base_lat;
	uint32_t sector_of_superpage;
	uint32_t device_sector;
	
	/* data */
	uint64_t CPU_FREQ;
	// bitmap ro record used lba --> check bitmap --> if 0, set bit[LBA]=1 total_lba_cnt++.
	uint64_t total_lba_cnt;	
	uint64_t base_ublk_lat_us;
	uint64_t base_ublk_slat_us;
};
extern struct ublksrv_delay delay_info;
extern int ublksrv_delay_module(const struct ublksrv_io_desc *iod);
extern void ublk_get_cpu_frequency();
extern int ublk_get_cpu_frequency_by_tick();

#endif

