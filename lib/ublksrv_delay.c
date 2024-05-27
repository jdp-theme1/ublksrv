#include <config.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdint.h>
#include <stdio.h>
#include <bits/types.h>
#include <time.h>

// #include "ublksrv.h"
// #include "ublksrv_aio.h"
#include <sched.h>

#include "queue.h"
#include "ublksrv_delay.h"

struct ublksrv_delay_read
{
	/* data */
	uint32_t base;
	uint32_t p29;
	uint32_t p39;
	uint32_t p49;
	uint32_t p59;
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
	/* data */
	uint64_t CPU_FREQ;
	uint64_t last_end_lba;
	// bitmap ro record used lba --> check bitmap --> if 0, set bit[LBA]=1 total_lba_cnt++.
	uint64_t total_lba_cnt;
	uint16_t choas_index;
	double choas_learning_rate;
	double gc_prob;
	struct ublksrv_delay_read read_delay_table;
	struct ublksrv_delay_write write_delay_table;
	struct ublksrv_delay_gc gc_delay_table;
	struct ublksrv_delay_wl wl_delay_table;
};
static struct ublksrv_delay delay_info;

// uint64_t CPU_FREQ;

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

void ublk_delay_init_tables(){
	// Add read delay parameter
	delay_info.read_delay_table.base	=	10;
	delay_info.read_delay_table.p29		=	50;
	delay_info.read_delay_table.p39		=	200;
	delay_info.read_delay_table.p49		=	400;
	delay_info.read_delay_table.p59		=	800;

	// Add write delay parameter
	delay_info.write_delay_table.base	=	50;
	delay_info.write_delay_table.p29	=	500;
	delay_info.write_delay_table.p39	=	1000;
	delay_info.write_delay_table.p49	=	2000;
	delay_info.write_delay_table.p59	=	4000;

	// Add garbage collection delay parameter
	delay_info.gc_delay_table.base	=	5;
	delay_info.gc_delay_table.p29	=	5;
	delay_info.gc_delay_table.p39	=	5;
	delay_info.gc_delay_table.p49	=	5;
	delay_info.gc_delay_table.p59	=	5;

	// Add wear-leveling delay parameter
	delay_info.wl_delay_table.base	=	5;
	delay_info.wl_delay_table.p29	=	5;
	delay_info.wl_delay_table.p39	=	5;
	delay_info.wl_delay_table.p49	=	5;
	delay_info.wl_delay_table.p59	=	5;

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
	printf("Estimated CPU frequency: %.2f Hz\n", cpu_frequency);
	ublk_delay_init_tables();
	//return 0;
}

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
    // ublk_dbg(UBLK_DBG_IO_CMD, "CPU frequency: %ld\n", cpu_freq);
	// int startcpu = sched_getcpu();
	// int curcpu = sched_getcpu();
	// int endcpu = 0;
	uint64_t start_ticks = rdtsc();
	uint64_t end_ticks = start_ticks + delay * cpu_freq * 1e-6; //select tick by micro seconds
	uint64_t current_ticks = start_ticks;
	while(current_ticks <= end_ticks) {
		ublk_dbg(UBLK_DBG_IO_CMD,"delaying, %ld", current_ticks);
		current_ticks = rdtsc();
		//curcpu = sched_getcpu();
		//if(curcpu != startcpu)
		//	ublk_dbg(UBLK_DBG_IO_CMD, "CPU changed from %d to %d\n", startcpu, curcpu);
	}
	ublk_dbg(UBLK_DBG_IO_CMD, "Start tick %ld, end tick: %ld, cur_tick: %ld", start_ticks, end_ticks, current_ticks);
	//endcpu =sched_getcpu();
	//if(endcpu != startcpu)
	//		ublk_dbg(UBLK_DBG_IO_CMD, "CPU changed from %d to %d\n", startcpu, endcpu);
	
}
int ublksrv_io_delay(int ublk_op){
	int s = rand();
	uint32_t iodelay = 0;
	// Get Start tick
	//ublksrv_delay_us(rdtsc(), 100);
	switch (ublk_op) {
		case UBLK_IO_OP_FLUSH:
			break;
		case UBLK_IO_OP_WRITE_SAME:
		case UBLK_IO_OP_WRITE_ZEROES:
		case UBLK_IO_OP_DISCARD:
			break;
		case UBLK_IO_OP_READ:
			if(s%99999 == 0) 		iodelay = delay_info.read_delay_table.p59;
			else if(s%9999 == 0) 	iodelay = delay_info.read_delay_table.p49;
			else if(s%999 == 0) 	iodelay = delay_info.read_delay_table.p39;
			else if(s%99 == 0) 		iodelay = delay_info.read_delay_table.p29;
			else 					iodelay = delay_info.read_delay_table.base;
			break;
		case UBLK_IO_OP_WRITE: 
			if(s%99999 == 0) 		iodelay = delay_info.write_delay_table.p59;
			else if(s%9999 == 0) 	iodelay = delay_info.write_delay_table.p49;
			else if(s%999 == 0) 	iodelay = delay_info.write_delay_table.p39;
			else if(s%99 == 0) 		iodelay = delay_info.write_delay_table.p29;
			else 					iodelay = delay_info.write_delay_table.base;
			break;
		default:
			break;
	}
	return iodelay;
}

int ublksrv_delay_module(int ublk_op){
	uint32_t delaytime = 0;
	if(delay_info.CPU_FREQ==0) return -1;
	delaytime = ublksrv_io_delay(ublk_op);
	ublksrv_delay_us(delaytime);
	return 0;
}

