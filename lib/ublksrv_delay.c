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

// static inline uint64_t rdtsc(void) {
//     unsigned int lo, hi;
//     // Use inline assembly to read the TSC
//     __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
//     return ((uint64_t)hi << 32) | lo;
// }

// unsigned int ublksrv_get_current_tick() {
//     unsigned int ticks = rdtsc();
//     printf("CPU ticks: %u\n", ticks);
//     ublk_dbg(UBLK_DBG_IO_CMD, "CPU ticks: %u\n", ticks);
//     return ticks;
// }


static inline uint64_t rdtsc(void) {
	    uint32_t lo, hi;
	        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
		    return ((uint64_t)hi << 32) | lo;
}

uint64_t get_cpu_frequency() {
    struct timespec ts_start, ts_end;
    uint64_t start, end;
    uint64_t elapsed_ns;

    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    start = rdtsc();

    do {
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    elapsed_ns = (ts_end.tv_sec - ts_start.tv_sec) * 1000000000LL + (ts_end.tv_nsec - ts_start.tv_nsec);
    } while (elapsed_ns < 1000000000LL); // 忙等待 100 毫秒
    end = rdtsc();
    uint64_t elapsed_tsc = end - start;

    return (elapsed_tsc * 10); // 將 100 毫秒內的 TSC 週期數轉換為每秒的週期數
}

uint64_t
ublksrv_get_current_tick(){
    // uint64_t ticks_mhz = spdk_get_ticks_hz() / 0;
    // ublk_dbg(UBLK_DBG_IO_CMD, "CPU ticks_mhz: %ld\n", ticks_mhz);
    // uint64_t clock_tick = clock();
    // ublk_dbg(UBLK_DBG_IO_CMD, "CPU clock_tick: %ld\n", clock_tick);
    uint64_t cpu_frequency = get_cpu_frequency();
    printf("CPU frequency: %ld Hz\n", cpu_frequency);
    ublk_dbg(UBLK_DBG_IO_CMD, "CPU clock_tick: %ld\n", cpu_frequency);
    return cpu_frequency;
}
int ublksrv_delay_module(int ublk_op){
	int s = rand();
	// Get Start tick
    //ublksrv_get_current_tick();
    //usleep(1000000);
	/*switch (ublk_op) {
		case UBLK_IO_OP_FLUSH:
			break;
		case UBLK_IO_OP_WRITE_SAME:
		case UBLK_IO_OP_WRITE_ZEROES:
		case UBLK_IO_OP_DISCARD:
			break;
		case UBLK_IO_OP_READ:
			if(s%99999 == 0) usleep(100);
			else if(s%9999 == 0) usleep(50);
			else if(s%999 == 0) usleep(30);
			else if(s%99 == 0) usleep(10);
			else usleep(5);
			break;
		case UBLK_IO_OP_WRITE: 
			if(s%99999 == 0) usleep(500);
			else if(s%9999 == 0) usleep(300);
			else if(s%999 == 0) usleep(200);
			else if(s%99 == 0) usleep(150);
			else usleep(100);
			break;
		default:
			break;
	}*/
    //ublksrv_get_current_tick();
}

int ublk_delay_init(){

}