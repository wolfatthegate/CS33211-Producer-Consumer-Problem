/* https://github.com/wolfatthegate/CS33211-Assignment 
 * Author - Waylon Luo
 * Date - April 17, 2021
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#define NAME "/pcp-sharemem"
#define NUM 8
#define SIZE (NUM * sizeof(int))
#define BUFFER 2
#define THREAD_NUM 1
#define ITR 5

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <err.h> 
#include <errno.h>
#include <time.h>

struct ShmData {
	int x; 
	sem_t semEmpty;
	sem_t semFull;
	pthread_mutex_t mutexBuffer;
};

#endif /* PROTOCOL_H */
