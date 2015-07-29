#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>


static __inline__ unsigned long long rdtsc ()
{
  unsigned hi,lo;
  __asm__ __volatile__ ("rdtsc":"=a"(lo),"=d"(hi));
  return (( unsigned long long)lo)|(((unsigned long long)hi)<<32);
};

void spin_sleep(int i)
{
        for (volatile int i = 0; i < 500000*i; i++);
};

void mymemcpy(volatile char *d, char *s, int l)
{
  volatile char *t = d;
  for (;--l;s++,d++)*d=*s;
}

#define LLC_SIZE 20

#define POOL_SIZE_L1D_LINES 512
#define POOL_SIZE_L2_LINES 8192
// This can differ on your CPU. Check with CPUID or some system diagnostics tool
#define POOL_SIZE_LLC_LINES LLC_SIZE*1024*1024/64

#define DISRUPT_ON 0
#define WARMUP_ON 1

volatile static bool in_copy = true;

struct list_item
{
        unsigned long long int tick;
        list_item *next;
        unsigned char padding[48];
};

static int results[8*1024];
static int result = 0;

list_item *init_pool_impl(list_item *head, int N, int A, int B)
{
    int C = B;
    list_item *current = head;
    list_item *last = head;
//printf ("requested %i\n", N);
    for (int i = 0; i < N - 1; i++)
    {
        current->tick = 0;
        C = (A*C+B) % N;
        current->next = (list_item*)&(head[C]);
//printf ("set up = %i %x\n", i, current->next);
        last = current;
        current = current->next;
    }
    return last;
}

void init_pool(list_item *head, int N, int A, int B)
{
    int processed = 0;
    list_item *latest = 0;
    if (N < 131072) {
      init_pool_impl(head, N, A, B);
      return;
    }
    while (N >= 131072) {
      latest = init_pool_impl(head + processed, 131072, A, B);
      N -= 131072; processed += 131072;
      latest->next = head + processed;
    }
    latest = init_pool_impl(head + processed, N, A, B);
}

void warmup(list_item* current, int N)
{
    bool write = (N > POOL_SIZE_L2_LINES) ? true : false;
    for(int i = 0; i < N - 1; i++)
    {
        current = current->next;
        if (write)
                current->tick++;
    }
}

void measure(list_item* head, int N)
{
        unsigned long long i1, i2, avg = 0;

        for (int j = 0; j < 10; j++)
        {
                list_item* current = head;
#if WARMUP_ON
                while(in_copy) warmup(head, N);
#else
                while(in_copy) spin_sleep(1);
#endif
                i1 = rdtsc();
                for(int i = 0; i < N - 1; i++)
                {
//printf ("iterating %i %x\n", i, current);
//                      current->tick++;
                        current = current->next;
                }
                i2 = rdtsc();
                current->tick++;
                in_copy = true;
                avg += (i2-i1)/10;
        }
        results[result++] = avg/N;
}

void DisruptorThreadFunction( void *lpParam )
{
        volatile list_item *area1 = (list_item *)malloc(POOL_SIZE_LLC_LINES*64*2);
        volatile list_item *area2 = (list_item *)malloc(POOL_SIZE_LLC_LINES*64*2);
printf("copied %x\n", (char*)area1);

        while (true)
        {
                while(!in_copy) spin_sleep(1);
#if DISRUPT_ON
        mymemcpy((char*)area1, (char*)area2, POOL_SIZE_LLC_LINES*64*2);
        mymemcpy((char*)area2, (char*)area1, POOL_SIZE_LLC_LINES*64*2);
#else
                spin_sleep(10);
#endif
                in_copy = false;
        };
}

void MeasurementThreadFunction( void *lpParam )
{
        usleep(50);
        list_item *head = (list_item *)malloc(POOL_SIZE_LLC_LINES*64*60);
        if (head == 0) { printf ("malloc failed\n"); return; };

        printf("Measure L1\n");
        init_pool(head, POOL_SIZE_L1D_LINES, 509, 509);
        for (int i = 10; i < POOL_SIZE_L1D_LINES; i+=15) {
                warmup(head, i);
                measure(head, i);
        };
        results[result++] = -1;
        printf("Measure L2 - %i\n", results[result-2]);
        init_pool(head, POOL_SIZE_L2_LINES, 509, 509);
        for (int i = POOL_SIZE_L1D_LINES; i < POOL_SIZE_L2_LINES; i+=100) {
                warmup(head, i);
                measure(head, i);
        };
        results[result++] = -1;
        printf("Measure LLC - %i\n", results[result-2]);
        init_pool(head, POOL_SIZE_LLC_LINES, 509, 509);
        for (int i = POOL_SIZE_L2_LINES; i < POOL_SIZE_LLC_LINES*2; i+=20000) {
                warmup(head, i);
                measure(head, i);
        };
        results[result++] = -1;

        printf("Measure DRAM - %i\n", results[result-2]);
        init_pool(head, POOL_SIZE_LLC_LINES*5, 509, 509);
        warmup(head, POOL_SIZE_LLC_LINES*5);
        measure(head, POOL_SIZE_LLC_LINES*5);
        measure(head, POOL_SIZE_LLC_LINES*5);


        free(head);
}

int main(int argc, char* argv[])
{
        unsigned long long nodeMask = 2;
        int iret1, iret2;
        pthread_t thread1, thread2;
        pthread_attr_t attr;

        cpu_set_t cpus;
        pthread_attr_init(&attr);

        CPU_ZERO(&cpus);
        CPU_SET(0, &cpus); // CPU0
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);

        iret1 = pthread_create( &thread1, &attr, DisruptorThreadFunction, 0);

        CPU_ZERO(&cpus);
        CPU_SET(3, &cpus); // CPU1
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
        pthread_create( &thread2, &attr, MeasurementThreadFunction, 0);

        pthread_join( thread2, NULL);

        for (int i = 0; i < result; i++)
                    printf("%i\n", results[i]);

        return 0;
}
