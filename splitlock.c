// disturber_splitlock.cpp : Defines the entry point for the console application.
//
#include <stdio.h>
#define TEST_COUNT 500000000


unsigned char array[256];
int main(int argc, char* argv[])
{
        volatile unsigned int *mylock;
volatile unsigned char *pointer;
for( pointer = array; ((unsigned int)pointer % 64) != 62 ; pointer++ );

mylock = (unsigned int*)pointer;

        printf("%x\n", pointer);
        for (long long i = 0; i < TEST_COUNT; i++)
        {
        (*mylock)++;
        asm ("lock;incl (%0) " : "=r" (mylock));
        }
        return 0;
}
