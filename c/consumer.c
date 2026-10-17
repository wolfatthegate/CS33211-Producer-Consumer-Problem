/* https://github.com/wolfatthegate/CS33211-Assignment 
 * Author - Waylon Luo
 * Date - April 17, 2021
 */

#include "protocol.h"
  
// consumer Code 
void* consumer(struct ShmData *shmData) {
    
    for(int i = 0 ; i < (ITR+3); i++) {

        sleep(2); 
        // semaphore wait when buffer is empty
        sem_wait(&shmData->semFull);
        // mutex lock
        pthread_mutex_lock(&shmData->mutexBuffer);
        // Item consumed (critical section)
        printf("Consumer consumes item %d\n", shmData->x);  
        shmData->x = (shmData->x) - 1; // x--;
        // mutex unlock
        pthread_mutex_unlock(&shmData->mutexBuffer);
        // semaphore notify when buffer is not empty
        sem_post(&shmData->semEmpty);
        
    }
}

int main()
{   
    // sleeping 1 second gives producer gives a chance to 
    // create shared memory space
    sleep(1); 
    // shared memory access
    int fd = shm_open(NAME, O_EXCL | O_RDWR, 0600); 

    if (fd < 0) {
        perror("shm_open() consumer error\n"); 
        close(fd); 
        shm_unlink(NAME); 
        return EXIT_FAILURE; 
    }

    ftruncate(fd, SIZE); 

    // mapping shared memory data as a structure
    struct ShmData *shmData = mmap(0, sizeof(*shmData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); 


    pthread_t th[THREAD_NUM];

    for (int i = 0; i < THREAD_NUM; i++) {
        printf("consumer thread creaded..\n");
        if (pthread_create(&th[i], NULL, &consumer, shmData) != 0) {
            perror("Failed to create thread\n");
        }
    }
    for (int i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(th[i], NULL) != 0) {
            perror("Failed to join thread\n");
        }
    }

    // destory semaphore and mutex
    sem_destroy(&shmData->semEmpty);
    sem_destroy(&shmData->semFull);
    pthread_mutex_destroy(&shmData->mutexBuffer);

    // unmap shared data and unlink shared memory
    munmap(shmData, SIZE); 
    close(fd); 
    shm_unlink(NAME); 
    return EXIT_SUCCESS; 
}
