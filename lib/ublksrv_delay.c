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
	uint32_t sector_of_superpage;
	uint32_t device_sector;
	
	/* data */
	uint64_t CPU_FREQ;
	// bitmap ro record used lba --> check bitmap --> if 0, set bit[LBA]=1 total_lba_cnt++.
	uint64_t total_lba_cnt;
	
	uint64_t base_ublk_lat_us;
	uint64_t base_ublk_slat_us;
	/* Read */
	uint8_t rate_read_page_fault;
	uint8_t rate_read_low_page;
	uint8_t rate_read_mid_page;
	uint8_t rate_read_high_page;
	uint8_t rate_read_channel_contention;
	float	lat_read_channel_contention;
	uint32_t rd_cache_sector;
	uint64_t nr_cur_read_size;
	uint64_t last_read_start_lba;
	uint64_t last_read_end_lba;
	uint64_t lat_sys_page_read_us;
	uint64_t lat_read_low_page_us;
	uint64_t lat_read_mid_page_us;
	uint64_t lat_read_high_page_us;
	uint64_t size_single_mapping_table;
	/* Write */
	uint32_t wr_remain_sectors;
	uint64_t wr_cache_sectors;
	uint32_t lat_write_pages_us;
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
	delay_info.last_read_start_lba = 0;
	delay_info.last_read_end_lba = 0;
	delay_info.base_lat = 50;

	delay_info.device_sector = 512;
	delay_info.sector_of_superpage=512*KB/delay_info.device_sector;

	delay_info.lat_sys_page_read_us = 80;
	delay_info.base_ublk_lat_us = 3;
	delay_info.base_ublk_slat_us = 4;
	delay_info.size_single_mapping_table=32*MB/delay_info.device_sector;

	/*Following parameters for Read*/
	delay_info.rate_read_low_page=20;
	delay_info.rate_read_mid_page=30;
	delay_info.rate_read_high_page=100-delay_info.rate_read_low_page-delay_info.rate_read_mid_page;

	delay_info.lat_read_low_page_us = 70;
	delay_info.lat_read_mid_page_us = 90;
	delay_info.lat_read_high_page_us = 130;	
	
	delay_info.rate_read_page_fault = 50;
	delay_info.rd_cache_sector=64*1024/delay_info.device_sector;
	delay_info.read_delay_table.seq_chunk_size = 64*KB/delay_info.device_sector;
	
	delay_info.rate_read_channel_contention = 10;
	delay_info.lat_read_channel_contention = 1.6;
	/* Following parameters for Write*/
	delay_info.wr_remain_sectors = 0;
	delay_info.wr_cache_sectors = 64*1024/delay_info.device_sector;	
	delay_info.lat_write_pages_us = 100;
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

int ublksrv_lat_pages_read(uint32_t nr_sectors, uint64_t start_addr, uint64_t cur_blksize){
	int iodelay = 0;
	ublk_dbg(UBLK_DBG_IO_CMD,"start_addr=%ld, nr_sectors=%d, delay_info.last_read_start_lba=%ld", start_addr, nr_sectors, delay_info.last_read_start_lba);
	if(nr_sectors>=delay_info.sector_of_superpage){ 		//Full channel utilization
		iodelay+=280;
	} else if(cur_blksize>32*KB && cur_blksize<=64*KB){	//32~64K latency
		iodelay+=35;
	} else if(cur_blksize>16*KB && cur_blksize<=32*KB){	//16~32K latency
		iodelay+=40;
	} else if(cur_blksize>8*KB && cur_blksize<=16*KB){	//8~16K latency
		iodelay+=30;
	} else if(cur_blksize>4*KB && cur_blksize<=8*KB){	//4~8K latency
		iodelay+=25;
	} else if(cur_blksize>512 && cur_blksize<=4*KB){	//512B~4K latency
		iodelay+=17;
	} else if(cur_blksize==512){						//512B latency
		iodelay+=19;
	}
	
	if(start_addr!=delay_info.last_read_start_lba)
			delay_info.nr_cur_read_size+=cur_blksize;
	if(delay_info.nr_cur_read_size>delay_info.rd_cache_sector){
		uint32_t s = rand()%100;
		ublk_dbg(UBLK_DBG_IO_CMD,"ublk: s=%d, Read flash",s);
		if(s>=delay_info.rate_read_high_page){
			iodelay+=delay_info.lat_read_high_page_us;
		} else if (s>=delay_info.rate_read_mid_page && s<delay_info.rate_read_high_page){
			iodelay+=delay_info.lat_read_mid_page_us;
		} else {
			iodelay+=delay_info.lat_read_mid_page_us;
		}
		delay_info.nr_cur_read_size -= delay_info.rd_cache_sector; /*Reset record value*/
	}
	ublk_dbg(UBLK_DBG_IO_CMD,"ublk ddlay: iodelay = %d, Delay delay_info.nr_cur_read_size=%ld", iodelay,delay_info.nr_cur_read_size);
	return iodelay;
}
int ublksrv_lat_pages_write(uint32_t nr_sectors, uint64_t start_addr, uint64_t cur_blksize, uint64_t wr_remain_sectors){
	int iodelay = 0;
	uint64_t totalsector = delay_info.wr_remain_sectors + nr_sectors;
	uint32_t sector_quo = totalsector / delay_info.sector_of_superpage;
	uint32_t sector_rem = totalsector % delay_info.sector_of_superpage;
	ublk_dbg(UBLK_DBG_IO_CMD,"start_addr=%ld, nr_sectors=%d, delay_info.sector_of_superpage= %d, sector_quo=%d", start_addr, nr_sectors, delay_info.sector_of_superpage, sector_quo);
	/* Base latency injection */
	//if(wr_remain_sectors+nr_sectors<delay_info.wr_cache_sectors){
	if(nr_sectors>delay_info.sector_of_superpage){ 		//Full channel utilization
		iodelay+=(181*(nr_sectors/delay_info.sector_of_superpage));
	} else if(cur_blksize>256*KB && nr_sectors<=delay_info.sector_of_superpage){	//256~512K latency
		iodelay+=181;
	} else if(cur_blksize>128*KB && cur_blksize<=256*KB){	//128~256K latency
		iodelay+=100;
	} else if(cur_blksize>64*KB && cur_blksize<=128*KB){	//64~128K latency
		iodelay+=58;
	} else if(cur_blksize>32*KB && cur_blksize<=64*KB){	//32~64K latency
		iodelay+=37;
	} else if(cur_blksize>16*KB && cur_blksize<=32*KB){	//16~32K latency --> TBD
		iodelay+=26;
	} else if(cur_blksize>8*KB && cur_blksize<=16*KB){	//8~16K latency --> TBD
		iodelay+=14;
	} else if(cur_blksize>4*KB && cur_blksize<=8*KB){	//4~8K latency
		iodelay+=14;
	} else if(cur_blksize>512 && cur_blksize<=4*KB){	//512B~4K latency
		iodelay+=13;
	} else {						//512B latency
		iodelay+=10;
	}
	//} else {
	//	iodelay += delay_info.lat_write_pages_us;
	//}
	/* Real programming latency injection */
	if(sector_quo>=1 && iodelay<100){
		ublk_log("wdelay add program latency");
		iodelay+=100;
	}
	/* BKOPS --> GC */

	/* BKOPS --> Wear-leveling */

	/* Update pending write blocks*/
	delay_info.wr_remain_sectors = sector_rem;
	ublk_dbg(UBLK_DBG_IO_CMD,"ublk wdelay: iodelay = %d, start_addr=%ld, nr_sectors=%d sector_quo=%d, delay_info.wr_remain_sectors=%d"
								, iodelay, start_addr, nr_sectors, sector_quo, delay_info.wr_remain_sectors);
	return iodelay;
}
int ublksrv_io_delay_cal(uint32_t ublk_op, uint32_t nr_sectors, uint64_t start_addr){
	uint32_t s = 0;
	int iodelay = 0;
	uint64_t cur_blksize = nr_sectors * delay_info.device_sector;
	srand(time(NULL)); //Jeff add for increase randomizer reliability
	//ublk_log("Start: total delay = %d, delay_info.remain_sectors = %d, totalblk = %ld, sector_quo = %d, sector_rem = %d", iodelay, delay_info.remain_sectors,totalblk, sector_quo, sector_rem);
	switch (ublk_op) {
		case UBLK_IO_OP_FLUSH:
			break;
		case UBLK_IO_OP_WRITE_SAME:
		case UBLK_IO_OP_WRITE_ZEROES:
		case UBLK_IO_OP_DISCARD:
			break;
		case UBLK_IO_OP_READ:		
			//ublk_dbg(UBLK_DBG_IO_CMD,"ublk: Delay add start\n");
			if(start_addr == delay_info.last_read_end_lba || 
				start_addr == delay_info.last_read_start_lba){ //Sequential Read
				ublk_dbg(UBLK_DBG_IO_CMD,"ublk: Delay add start for sequential read\n");
				iodelay += ublksrv_lat_pages_read(nr_sectors, start_addr, cur_blksize); // Add measured latency by page size
			} else { //Random Read
				ublk_dbg(UBLK_DBG_IO_CMD,"ublk: Delay add start for random read\n");
				s = 100-rand()%100; 
				iodelay += ublksrv_lat_pages_read(nr_sectors, start_addr, cur_blksize);
				if(s >= delay_info.rate_read_page_fault){	/* page is not in cache */
					iodelay += (delay_info.lat_sys_page_read_us);
				} else if(s >= delay_info.rate_read_channel_contention){ /* Channel contention proability */
					iodelay *= delay_info.lat_read_channel_contention;
				}
			}
			iodelay -= (delay_info.base_ublk_lat_us + delay_info.base_ublk_slat_us); //Correct the latency which induced by basic operation and s_lat << KCC

			/* update start/end lba */
			delay_info.last_read_start_lba=start_addr;
			delay_info.last_read_end_lba=start_addr+nr_sectors;

			/* Restrict IOPS for ensure the latency boundary*/
			if (iodelay<10){
				iodelay=10; /* Restrict IOPS, should add latency accorrding to block size*/
				ublk_dbg(UBLK_DBG_IO_CMD,"ublk: Delay number is negtive, RESET delay latency to 0\n");
			}
			break;
		case UBLK_IO_OP_WRITE: 
			/*iodelay += delay_info.base_lat;
			s = rand()%100;	
			iodelay += ((uint64_t)(676*log(s)+880)*sector_quo);*/
			iodelay+=ublksrv_lat_pages_write(nr_sectors, start_addr, cur_blksize, delay_info.wr_remain_sectors);
			iodelay -= (delay_info.base_ublk_lat_us + delay_info.base_ublk_slat_us); //Correct the latency which induced by basic operation and s_lat << KCC
			if (iodelay<10){
				iodelay=10; /* Restrict IOPS, should add latency accorrding to block size*/
				ublk_dbg(UBLK_DBG_IO_CMD,"ublk: Delay number is negtive, RESET delay latency to 0\n");
			}
			break;
		default:
			break;
	}

	// ublk_dbg(UBLK_DBG_IO_CMD,"end = total delay = %d, delay_info.remain_sectors = %d, totalblk = %ld, sector_quo = %d, sector_rem = %d\n", iodelay, delay_info.remain_sectors,totalblk, sector_quo, sector_rem);	
	// ublk_log("End: total ddlay = %d, delay_info.remain_sectors = %d, totalblk = %ld, sector_quo = %d, sector_rem = %d\n", iodelay, delay_info.remain_sectors,totalblk, sector_quo, sector_rem);
	return iodelay;
}

int ublksrv_delay_module(const struct ublksrv_io_desc *iod){
	uint32_t delaytime = 0;
	if(delay_info.CPU_FREQ==0) return -1;
	ublk_dbg(UBLK_DBG_IO_CMD, "start_sector %lld, nr_sectors: %d, op_flags: %d", iod->start_sector, iod->nr_sectors, iod->op_flags);	
	uint32_t ublk_op = ublksrv_get_op(iod);
	/* Start Cal latency*/
	delaytime = ublksrv_io_delay_cal(ublk_op, iod->nr_sectors, iod->start_sector);
	ublksrv_delay_us(delaytime);
	return 0;
}

