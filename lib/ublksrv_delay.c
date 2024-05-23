#include <config.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdint.h>
#include <stdio.h>
#include <bits/types.h>
#include <time.h>

#include "ublksrv_priv.h"
#include "ublksrv_aio.h"
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
int ublk_get_cpu_frequency() {
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

void ublksrv_delay_ns(uint64_t delay){
    ublk_dbg(UBLK_DBG_IO_CMD, "CPU frequency: %ld\n", CPU_FREQ);
	uint64_t start_ticks = rdtsc();
	uint64_t end_ticks = start_ticks + delay * CPU_FREQ * 1e-9;
	uint64_t current_ticks = rdtsc();
	while(current_ticks <= end_ticks) {
		ublk_log("delaying, %ld", current_ticks);
		current_ticks = rdtsc();
	}
	ublk_log("Start tick %ld, end tick: %ld, cur_tick: %ld", start_ticks, end_ticks, current_ticks);
}

void ublksrv_delay_us(uint64_t delay){
    ublk_dbg(UBLK_DBG_IO_CMD, "CPU frequency: %ld\n", CPU_FREQ);
	uint64_t start_ticks = rdtsc();
	uint64_t end_ticks = start_ticks + delay * CPU_FREQ * 1e-6;
	uint64_t current_ticks = start_ticks;
	while(current_ticks <= end_ticks) {
		ublk_dbg(UBLK_DBG_IO_CMD,"delaying, %ld", current_ticks);
		current_ticks = rdtsc();
	}
	ublk_log("Start tick %ld, end tick: %ld, cur_tick: %ld", start_ticks, end_ticks, current_ticks);
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
			/*if(s%99999 == 0) ublksrv_delay_us(100);
			else if(s%9999 == 0) ublksrv_delay_us(50);
			else if(s%999 == 0) ublksrv_delay_us(30);
			else if(s%99 == 0) ublksrv_delay_us(10);
			else ublksrv_delay_us(5);*/
			ublksrv_delay_us(100);
			break;
		case UBLK_IO_OP_WRITE: 
			/*if(s%99999 == 0) ublksrv_delay_us(500);
			else if(s%9999 == 0) ublksrv_delay_us(300);
			else if(s%999 == 0) ublksrv_delay_us(200);
			else if(s%99 == 0) ublksrv_delay_us(150);
			else ublksrv_delay_us(100);*/
			ublksrv_delay_us(200);
			break;
		default:
			break;
	}
    //ublksrv_get_current_tick();
}

int ublk_delay_init(){

}