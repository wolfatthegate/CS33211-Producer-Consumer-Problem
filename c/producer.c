/* https://github.com/wolfatthegate/CS33211-Assignment 
 * Author - Waylon Luo
 * Date - April 17, 2021
 */
 
#include "protocol.h"

// producer Code

void* producer(struct ShmData *shmData) {
    
    for(int i = 0 ; i < ITR; i++) {

        sleep(1);
        // semaphore wait when buffer is full
        sem_wait(&shmData->semEmpty);
        // mutex lock
        pthread_mutex_lock(&shmData->mutexBuffer);
        // Item produced (critical section)
        shmData->x = (shmData->x) + 1; //x++
        printf("Producer produces item %d\n", shmData->x); 
        // mutex unlock
        pthread_mutex_unlock(&shmData->mutexBuffer);
        // semaphore notify when buffer is not full
        sem_post(&shmData->semFull);
        
    }

}

int main()
{
    // create shared memory space
    int fd = shm_open(NAME, O_CREAT | O_EXCL | O_RDWR, 0600); 
    if (fd < 0) {
        perror("shm_open() producer error\n"); 
        close(fd); 
        shm_unlink(NAME); 
        return EXIT_FAILURE; 
    }

    ftruncate(fd, SIZE); 
    // mapping shared memory data as a structure
    struct ShmData *shmData = mmap(0, sizeof(*shmData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); 

    // initializing struct data
    shmData->x     = 0; // Item number
    pthread_mutex_init(&shmData->mutexBuffer, NULL);
    sem_init(&shmData->semEmpty, 0, BUFFER);
    sem_init(&shmData->semFull, 0, 0);

    pthread_t th[THREAD_NUM];

    for (int i = 0; i < THREAD_NUM; i++) {
        printf("producer thread creaded..\n");
        if (pthread_create(&th[i], NULL, &producer, shmData) != 0) {
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
