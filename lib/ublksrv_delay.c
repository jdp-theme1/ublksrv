#include <config.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdint.h>
#include <stdio.h>
#include <bits/types.h>
#include <time.h>
#include <math.h> // KCC for practice Gauss Dist
#include "ublksrv.h"
// #include "ublksrv_aio.h"
#include <sched.h>

#include "queue.h"
#include "ublksrv_delay.h"

#define KB (1024UL)
#define MB (1024*1024UL)
struct ublksrv_delay_read
{
	/* data */
	uint32_t base;
	uint32_t p29;
	uint32_t p39;
	uint32_t p49;
	uint32_t p59;
	uint32_t seq_chunk_size;
};
struct ublksrv_delay_write
{
	/* data */
	uint32_t base;
	uint32_t p29;
	uint32_t p39;
	uint32_t p49;
	uint32_t p59;
};
struct ublksrv_delay_gc
{
	/* data */
	uint32_t base;
	uint32_t p29;
	uint32_t p39;
	uint32_t p49;
	uint32_t p59;
};
struct ublksrv_delay_wl
{
	/* data */
	uint32_t base;
	uint32_t p29;
	uint32_t p39;
	uint32_t p49;
	uint32_t p59;
};


struct ublksrv_delay
{
	/* device information*/
	uint16_t choas_index;
	uint16_t base_lat;
	uint32_t size_of_superpage;
	uint32_t device_sector;
	uint32_t remain_sectors;
	/* data */
	uint64_t CPU_FREQ;
	uint64_t last_end_lba;
	// bitmap ro record used lba --> check bitmap --> if 0, set bit[LBA]=1 total_lba_cnt++.
	uint64_t total_lba_cnt;
	uint64_t base_slc_page_read_us;
	uint64_t base_ublk_lat_us;
	uint64_t base_ublk_slat_us;
	uint64_t base_low_page_us;
	uint64_t base_mid_page_us;
	uint64_t base_high_page_us;
	double choas_learning_rate;
	double gc_prob;
	struct ublksrv_delay_read read_delay_table;
	struct ublksrv_delay_write write_delay_table;
	struct ublksrv_delay_gc gc_delay_table;
	struct ublksrv_delay_wl wl_delay_table;
};

static struct ublksrv_delay delay_info;

double gaussrand() {
	static double U, V;
	static int phase = 0;
	double Z;
	double PI = 3.14159265358979323846;
	if(phase == 0){
		U = rand() / (RAND_MAX + 1.0);
		V = rand() / (RAND_MAX + 1.0);
		Z = sqrt(-2.0 * log(U))* sin(2.0 * PI * V);
	}
	else
		Z = sqrt(-2.0 * log(U)) * cos(2.0 * PI * V);
	phase = 1 - phase;
	return Z;
}

uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__ (
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return ((uint64_t)hi << 32) | lo;
}

void get_time(struct timespec* ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
}

/*KCC add for Get CPU freq*/
void set_cpu_affinity() {
	cpu_set_t cpuset;
	pthread_t current_thread = pthread_self();

	// init CPU set and join cpu 0 into cpuset
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
 
	// set affinity for current thread
	int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
	if (ret != 0) {
		perror("pthread_setaffinity_np");
	}
}

void XPG_S50_PRO_1TB(){
	/*struct ublksrv_ctrl_dev_info *devinfo = dev->dev_info;
	printf("***---  %d\n", devinfo->max_io_buf_bytes);*/
	delay_info.choas_index = 0;
	delay_info.last_end_lba = 0;
	delay_info.base_lat = 50;

	delay_info.device_sector = 512;
	delay_info.size_of_superpage=512*KB/delay_info.device_sector;

	delay_info.base_slc_page_read_us = 80;
	delay_info.base_ublk_lat_us = 3;
	delay_info.base_ublk_slat_us = 4;
	delay_info.base_low_page_us = 70;
	delay_info.base_mid_page_us = 90;
	delay_info.base_high_page_us = 130;
	/*Following parameters for Seq Read*/
	delay_info.read_delay_table.seq_chunk_size = 64*KB/delay_info.device_sector;
	
	/* Following parameters for Write*/
	delay_info.remain_sectors = 0;
	
}

void ublk_delay_init_tables(){
	XPG_S50_PRO_1TB();
}

void ublk_get_cpu_frequency() {
	struct timespec start, end;
	uint64_t start_ticks, end_ticks;
	double elapsed_time, cpu_frequency;

//	set_cpu_affinity();

	get_time(&start);
	start_ticks = rdtsc();
	sleep(1);
	get_time(&end);
	end_ticks = rdtsc();

	elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	cpu_frequency = (end_ticks - start_ticks) / elapsed_time;
	delay_info.CPU_FREQ = (uint64_t)cpu_frequency;
	ublk_dbg(UBLK_DBG_IO_CMD, "Estimated CPU frequency: %ld\n", delay_info.CPU_FREQ);
	// printf("Estimated CPU frequency: %.2f Hz\n", cpu_frequency);
	ublk_delay_init_tables();
	//return 0;
}
/*
int ublksrv_delay_module_init(struct ublksrv_ctrl_dev *dev) {
	struct ublksrv_ctrl_dev_info *info = &dev->dev_info;
	int i, ret;
	struct ublk_params p;
	ublk_dbg(UBLK_DBG_DEV, "Delay module enabled... Start to get CPU clock rate\n");
	ublk_get_cpu_frequency();
	printf("dev id %d: nr_hw_queues %d queue_depth %d block size %d dev_capacity %lld\n",
		 info->dev_id,
	                info->nr_hw_queues, info->queue_depth,
	                1 << p.basic.logical_bs_shift, p.basic.dev_sectors);
					
	return 0;
}
*/
void ublksrv_delay_ns(uint64_t delay){
	uint64_t cpu_freq = delay_info.CPU_FREQ;
    ublk_dbg(UBLK_DBG_IO_CMD, "CPU frequency: %ld\n", cpu_freq);
	uint64_t start_ticks = rdtsc();
	uint64_t end_ticks = start_ticks + delay * cpu_freq * 1e-9; //select tick by nano seconds
	uint64_t current_ticks = rdtsc();
	while(current_ticks <= end_ticks) {
		ublk_log("delaying, %ld", current_ticks);
		current_ticks = rdtsc();
	}
	ublk_dbg(UBLK_DBG_IO_CMD, "Start tick %ld, end tick: %ld, cur_tick: %ld", start_ticks, end_ticks, current_ticks);
}

void ublksrv_delay_us(uint64_t delay){
	uint64_t cpu_freq = delay_info.CPU_FREQ;
	uint64_t start_ticks = rdtsc();
	uint64_t end_ticks = start_ticks + (uint64_t)((float)(delay) * 1e-6 * cpu_freq); //select tick by micro seconds
	uint64_t current_ticks = start_ticks;
	while(current_ticks <= end_ticks) {
		current_ticks = rdtsc();
	}
	// ublk_dbg(UBLK_DBG_IO_CMD, "Start tick %ld, end tick: %ld, cur_tick: %ld", start_ticks, end_ticks, current_ticks);
}

    //ublksrv_io_delay(ublk_op, iod->start_sector, iod->start_sector);  
int ublksrv_io_delay(uint32_t ublk_op, uint32_t nr_sectors, uint64_t start_addr){
	uint32_t s = 0;
	int iodelay = 0;
	// uint32_t nr_sectors = iod->nr_sectors;
	// uint32_t start_addr = iod->start_sector;
	uint32_t cur_blksize = nr_sectors * delay_info.device_sector;
	uint64_t totalblk = nr_sectors + delay_info.remain_sectors;
	uint32_t sector_quo = totalblk / delay_info.size_of_superpage;
	uint32_t sector_rem = totalblk % delay_info.size_of_superpage;
	// srand(time(NULL)); //Jeff add
	// ublk_log("Start = total delay = %d, delay_info.remain_sectors = %d, totalblk = %ld, sector_quo = %d, sector_rem = %d", iodelay, delay_info.remain_sectors,totalblk, sector_quo, sector_rem);
	switch (ublk_op) {
		case UBLK_IO_OP_FLUSH:
			break;
		case UBLK_IO_OP_WRITE_SAME:
		case UBLK_IO_OP_WRITE_ZEROES:
		case UBLK_IO_OP_DISCARD:
			break;
		case UBLK_IO_OP_READ:		
			if(cur_blksize < 4*KB){
					s = rand()%10000;	
					iodelay+=19;
					if(start_addr%128==0){
						int temp = ((start_addr/128)%24);
						if(temp<=8) iodelay+=delay_info.base_high_page_us;
						else if(temp>8 && temp<=16) iodelay+=delay_info.base_mid_page_us;
						else iodelay+=delay_info.base_low_page_us;
					}
			}
			iodelay -= (delay_info.base_ublk_lat_us + delay_info.base_ublk_slat_us);
			break;
		case UBLK_IO_OP_WRITE: 
			iodelay += delay_info.base_lat;
			s = rand()%100;	
			iodelay += ((uint64_t)(676*log(s)+880)*sector_quo);
			delay_info.remain_sectors = sector_rem;
			break;
		default:
			break;
	}
	if (iodelay<0){
		iodelay=0;
		ublk_dbg(UBLK_DBG_IO_CMD,"ublk: Delay number is negtive, RESET delay latency to 0\n");
	}
		
	ublk_dbg(UBLK_DBG_IO_CMD,"end = total delay = %d, delay_info.remain_sectors = %d, totalblk = %ld, sector_quo = %d, sector_rem = %d", iodelay, delay_info.remain_sectors,totalblk, sector_quo, sector_rem);
	return iodelay;
}

int ublksrv_delay_module(const struct ublksrv_io_desc *iod){
	uint32_t delaytime = 0;
	if(delay_info.CPU_FREQ==0) return -1;

	ublk_dbg(UBLK_DBG_IO_CMD, "start_sector %lld, nr_sectors: %d, op_flags: %d", iod->start_sector, iod->nr_sectors, iod->op_flags);	
	uint32_t ublk_op = ublksrv_get_op(iod);
	delaytime = ublksrv_io_delay(ublk_op, iod->nr_sectors, iod->start_sector);
	ublk_dbg(UBLK_DBG_IO_CMD, "io_delay = %d, cache_delay = %d", delaytime, delay_info.base_lat);

	ublksrv_delay_us(delaytime);
	delay_info.last_end_lba = iod->start_sector+iod->nr_sectors; /* cal last lba*/
	return 0;
}

