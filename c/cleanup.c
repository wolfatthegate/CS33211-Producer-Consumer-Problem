/* https://github.com/wolfatthegate/CS33211-Assignment 
 * Author - Waylon Luo
 * Date - April 17, 2021
 */

#include "protocol.h"

int main(){
        // unmap shared data and unlink shared memory
        // if either producer.c and consumer.c crashes
    int fd = shm_open(NAME, O_EXCL | O_RDWR, 0600);

    perror("shm_open() is already open closing now.\n");
    close(fd);
    shm_unlink(NAME);

    return EXIT_SUCCESS;
}