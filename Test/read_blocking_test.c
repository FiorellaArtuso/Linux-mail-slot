#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio_ext.h>
#include <pthread.h>

#include "const.h"

void *write_thread(void *args) {
    sleep(30);
    int fd = *(int*)args;
    write(fd, "test", 5);
}

void *read_thread(void* args) {
    int fd = *(int*)args;
    char read_buf[MAX_DATA_UNIT_SIZE];
    printf("blocking read called on an empty maislot\n");
    read(fd, read_buf, MAX_DATA_UNIT_SIZE);
    printf("reader woken up\n");
}


int main(int argc, char** argv){
    int ret;
    char read_buf[MAX_DATA_UNIT_SIZE];
    pthread_t thread_write;
    pthread_t thread_read;

	if(argc!=3){
		printf("you should pass MAJOR number and MINOR number as parameters\n");
		return -1;
	}

	int major = atoi(argv[1]);
	int minor = atoi(argv[2]);
	dev_t device = makedev(major, minor);

    char pathname[80];
    sprintf(pathname,"/dev/mailslot%d", minor);

	if( mknod(pathname, S_IFCHR|0666, device) == -1){
		if(errno == EEXIST)
			printf("Pathname '%s' already exists\n",pathname);
		else{
			printf("ERROR in the creation of the file %s: %s\n", pathname, strerror(errno));
			return -1;
            }
        }

	int fd = open(pathname, 0666);

	if(fd == -1){
		printf("ERROR while opening the file %s: %s\n", pathname, strerror(errno));
		return -1;
        }

    while(ioctl(fd,GET_FREESPACE_SIZE_CTL) < MAX_STORAGE){
        read(fd, read_buf, MAX_DATA_UNIT_SIZE);
    }

    if(pthread_create(&thread_read, NULL, read_thread, (void*)&fd)) {
        fprintf(stderr, "Error creating thread\n");
        return -1;
        }

    if(pthread_create(&thread_write, NULL, write_thread, (void*)&fd)) {
        fprintf(stderr, "Error creating thread\n");
        return -1;
        }

    if(pthread_join(thread_write, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return -1;
        }
    if(pthread_join(thread_read, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return -1;
        }

    close(fd);
    }
