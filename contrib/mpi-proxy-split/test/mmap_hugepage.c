#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <mpi.h>
#define NUM_PAGES 100

int main(){
  void *addr;
  printf("Program is up\n Attach gdb\n");
  fflush(stdout);
  volatile int dummy=1;
  //while(dummy);
  MPI_Init(NULL,NULL);
  int length=1024*1024*4;
  for(int i=0;i<NUM_PAGES;i++){
    addr = mmap(NULL, length, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB, -1, 0);
    if (addr == MAP_FAILED){
      printf("mmap_failed\n");
    }
    else{
      printf("mmap successful: %d\n",i+1);
    }
    fflush(stdout);
    sleep(5);
  }
 // sleep(600);
}
