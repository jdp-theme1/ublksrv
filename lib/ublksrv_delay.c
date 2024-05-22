#include <config.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdint.h>
#include <stdio.h>

#include "ublksrv_priv.h"
#include "ublksrv_aio.h"
#include "queue.h"

static inline uint64_t rdtsc(void) {
    unsigned int lo, hi;
    // Use inline assembly to read the TSC
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

unsigned int ublksrv_get_current_tick() {
    unsigned int ticks = rdtsc();
    printf("CPU ticks: %u\n", ticks);
    ublk_dbg(UBLK_DBG_IO_CMD, "CPU ticks: %u\n", ticks);
    return ticks;
}
// 獲取 CPU 時鐘頻率
unsigned long long get_cpu_frequency() {
    struct timespec ts_start, ts_end;
    uint64_t start, end;
    // 獲取開始時間和開始 TSC
    clock_gettime(CLOCK_REALTIME, &ts_start);
    start = rdtsc();
    // 睡眠 1 秒
    sleep(1);
    // 獲取結束時間和結束 TSC
    clock_gettime(CLOCK_REALTIME, &ts_end);
    end = rdtsc();
    // 計算 TSC 增量
    uint64_t elapsed_tsc = end - start;
    return elapsed_tsc; // 每秒的 TSC 週期數
    }
    int main() {
    uint64_t cpu_frequency = get_cpu_frequency();
    printf("CPU frequency: %llu Hz\n", cpu_frequency);
    // 假設我們想要將微秒轉換為 TSC 週期數
    uint64_t us = 1000; // 例如，1000 微秒
    uint64_t ticks = (cpu_frequency * us) / 1000000;
    printf("%llu us corresponds to %llu ticks\n", us, ticks);
    return 0;
}
int ublksrv_delay_module(int ublk_op){
	int s = rand();
	// Get Start tick
    ublksrv_get_current_tick();
	switch (ublk_op) {
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
	}
}
