#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define NUM_PTS 9
#define ITERS 1000000

void *memcpy_avx(void *dest, const void *src, size_t n);
void *__memcpy_fwd(void *dest, const void *src, size_t n);

static int test_sizes[] = {4, 8, 16, 32, 64, 128, 256, 512, 1024};

static void* (*memcpy_funcs[3])(void *dest, const void *src, size_t n) = {
  memcpy_avx,
  __memcpy_fwd,
  memcpy,
};

static double get_time() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  double ret = tv.tv_usec;
  ret = (ret/1000);
  ret += (tv.tv_sec * 1000);

  return ret;
}

static double bench_memcpy(int mem_size, int no){
  double total_time = 0;
  void *src, *dest;
  src = malloc(mem_size*1024);
  dest = malloc(mem_size*1024);
  
  double start_t, end_t;
  start_t = get_time();
  for(int i=0; i<ITERS; i++){
    memcpy_funcs[no](dest, src, mem_size*1024);
  }
  end_t = get_time();
  
  free(src);
  free(dest);

  total_time = (end_t - start_t)/ITERS;
  return total_time;
}  

int main(int argc, char** argv){
  if (argc != 2){
    fprintf(stderr, "%s 0|1\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  double result_array[NUM_PTS];

  for(int i=0; i<NUM_PTS; i++){
    result_array[i] = bench_memcpy(test_sizes[i], (strtol(argv[1], NULL, 10)));
  }

  printf("{\"4KB\":%f,\"8KB\":%f,\"16KB\":%f,\"32KB\":%f,\"64KB\":%f,\"128KB\":%f,\"256KB\":%f,\"512KB\":%f,\"1024KB\":%f}\n",result_array[0], result_array[1],result_array[2],result_array[3],result_array[4], result_array[5], result_array[6], result_array[7], result_array[8]);
}
