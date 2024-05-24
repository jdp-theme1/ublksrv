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

#include "queue.h"
#include "ublksrv_delay.h"

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

static uint64_t CPU_FREQ;
/*KCC add for Get CPU freq*/
/*
int ublk_get_cpu_frequency_by_file() {
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    char buffer[1024];
    double frequency = 0.0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, "cpu MHz")) {
            sscanf(buffer, "cpu MHz : %lf", &frequency);
            break;
        }
    }

    fclose(fp);
	CPU_FREQ = frequency * 1e6; // transform to Hz
    //return frequency * 1e6; // transform to Hz
}
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
*/

int ublk_get_cpu_frequency() {
	struct timespec start, end;
	uint64_t start_ticks, end_ticks;
	double elapsed_time, cpu_frequency;

	// 設定當前執行緒的CPU親和性
	// set_cpu_affinity();

	// 取得開始時間和rdtsc值
	get_time(&start);
	start_ticks = rdtsc();

	// 等待1秒
	sleep(1);

	// 取得結束時間和rdtsc值
	get_time(&end);
	end_ticks = rdtsc();

	// 計算時間差
	elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	// 計算CPU頻率
	cpu_frequency = (end_ticks - start_ticks) / elapsed_time;
	CPU_FREQ = (uint64_t)cpu_frequency;
	ublk_dbg(UBLK_DBG_IO_CMD, "Estimated CPU frequency: %ld\n", CPU_FREQ);
	printf("Estimated CPU frequency: %.2f Hz\n", cpu_frequency);

	return 0;
}

void ublksrv_delay_ns(uint64_t delay){
    ublk_dbg(UBLK_DBG_IO_CMD, "CPU frequency: %ld\n", CPU_FREQ);
	uint64_t start_ticks = rdtsc();
	uint64_t end_ticks = start_ticks + delay * CPU_FREQ * 1e-9; //select tick by nano seconds
	uint64_t current_ticks = rdtsc();
	while(current_ticks <= end_ticks) {
		ublk_log("delaying, %ld", current_ticks);
		current_ticks = rdtsc();
	}
	ublk_dbg(UBLK_DBG_IO_CMD, "Start tick %ld, end tick: %ld, cur_tick: %ld", start_ticks, end_ticks, current_ticks);
}

void ublksrv_delay_us(uint64_t delay){
    ublk_dbg(UBLK_DBG_IO_CMD, "CPU frequency: %ld\n", CPU_FREQ);
	uint64_t start_ticks = rdtsc();
	uint64_t end_ticks = start_ticks + delay * CPU_FREQ * 1e-6; //select tick by micro seconds
	uint64_t current_ticks = start_ticks;
	while(current_ticks <= end_ticks) {
		ublk_dbg(UBLK_DBG_IO_CMD,"delaying, %ld", current_ticks);
		current_ticks = rdtsc();
	}
	ublk_dbg(UBLK_DBG_IO_CMD, "Start tick %ld, end tick: %ld, cur_tick: %ld", start_ticks, end_ticks, current_ticks);
}

int ublksrv_delay_module(int ublk_op){
	int s = rand();
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
			if(s%99999 == 0) ublksrv_delay_us(100);
			else if(s%9999 == 0) ublksrv_delay_us(50);
			else if(s%999 == 0) ublksrv_delay_us(30);
			else if(s%99 == 0) ublksrv_delay_us(10);
			else ublksrv_delay_us(5);
			//ublksrv_delay_us(100);
			break;
		case UBLK_IO_OP_WRITE: 
			if(s%99999 == 0) ublksrv_delay_us(5000);
			else if(s%9999 == 0) ublksrv_delay_us(3000);
			else if(s%999 == 0) ublksrv_delay_us(2000);
			else if(s%99 == 0) ublksrv_delay_us(1500);
			else ublksrv_delay_us(100);
			//ublksrv_delay_us(200);
			break;
		default:
			break;
	}
    //ublksrv_get_current_tick();
}

int ublk_delay_init(){

}
