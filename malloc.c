#include "trace.h"
#include "malloc.h"

uint64_t malloc_time = 0;
uint64_t calloc_time = 0;
uint64_t realloc_time = 0;
uint64_t free_time = 0;
uint64_t overhead_time = 0;

void print_malloc_stats(void)
{
	double mtime, ctime, rtime, ftime, otime, total;
	mtime = (double)malloc_time/1000000000;
	ctime = (double)calloc_time/1000000000;
	rtime = (double)realloc_time/1000000000;
	ftime = (double)free_time/1000000000;
	otime = (double)overhead_time/1000000000/4;
	total = mtime + ctime + rtime + ftime;
	
	printf("malloc_time:  %f\n", (double)malloc_time/1000000000);
	printf("calloc_time:  %f\n", (double)calloc_time/1000000000);
	printf("realloc_time: %f\n", (double)realloc_time/1000000000);
	printf("free_time:    %f\n", (double)free_time/1000000000);
	printf("overhead_time: %f\n", (double)overhead_time/1000000000);

	printf("\nMinus estimated clock measurement overhead:\n");
	printf("malloc_time:  %f\n", mtime - (mtime/total)*otime);
	printf("calloc_time:  %f\n", ctime - (ctime/total)*otime);
	printf("realloc_time: %f\n", rtime - (rtime/total)*otime);
	printf("free_time:    %f\n", ftime - (ftime/total)*otime);
	printf("total:        %f\n", total - otime);
}

void *__libc_malloc(size_t size);
void *__libc_calloc(size_t nmemb, size_t size);
void *__libc_realloc(void *ptr, size_t size);
void __libc_free(void *ptr);

void *malloc(size_t size)
{
	uint64_t start, end;
	void *ret;

	start = getnanotime();
	end = getnanotime();
	overhead_time += 4*(end-start);

	start = getnanotime();
	ret = __libc_malloc(size);
	end = getnanotime();
	malloc_time += (end - start);
	return ret;
}

void *calloc(size_t nmemb, size_t size)
{
	uint64_t start, end;
	void *ret;

	start = getnanotime();
	end = getnanotime();
	overhead_time += 4*(end-start);

	start = getnanotime();
	ret = __libc_calloc(nmemb, size);
	end = getnanotime();
	calloc_time += (end - start);
	return ret;
}

void *realloc(void *ptr, size_t size)
{
	uint64_t start, end;
	void *ret;

	start = getnanotime();
	end = getnanotime();
	overhead_time += 4*(end-start);

	start = getnanotime();
	ret = __libc_realloc(ptr, size);
	end = getnanotime();
	realloc_time += (end - start);
	return ret;
}

void free(void *ptr)
{
	uint64_t start, end;

	start = getnanotime();
	end = getnanotime();
	overhead_time += 4*(end-start);

	start = getnanotime();
	__libc_free(ptr);
	end = getnanotime();
	free_time += (end - start);
}
