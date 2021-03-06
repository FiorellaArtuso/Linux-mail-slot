#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio_ext.h>

#include "const.h"


int main(int argc, char** argv){
    int ret;
    char read_buf[MAX_DATA_UNIT_SIZE+1];

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

    // TEST 1
    printf("TEST 1: len = 0 - ");
    ret = write(fd, "test", 0);
    if (ret < 0)
        printf("PASSED\n");
    else
        printf("NOT PASSED\n");

    ioctl(fd, CHANGE_MAX_DATA_UNIT_SIZE_CTL, 2);

    // TEST 2
    printf("TEST 2: len > max_data_unit_size - ");
    ret = write(fd, "test", 5);
    if (ret < 0)
        printf("PASSED\n");
    else
        printf("NOT PASSED\n");


    // TEST 3
    printf("TEST 3: len = max_data_unit_size - ");
    ret = write(fd, "t", 2);
    if (ret > 0)
        printf("PASSED\n");
    else
        printf("NOT PASSED\n");

    ioctl(fd, CHANGE_MAX_DATA_UNIT_SIZE_CTL, 5);

    // TEST 4
    printf("TEST 4: len = max_data_unit_size - ");
    ret = write(fd, "test", 5);
    if (ret > 0)
        printf("PASSED\n");
    else
        printf("NOT PASSED\n");


    // TEST 5
    printf("TEST 5: len_to_read < first data unit size - ");
    ret = read(fd, read_buf, 1);
    if (ret < 0)
        printf("PASSED\n");
    else
        printf("NOT PASSED\n");

    // TEST 6
    printf("TEST 6: len_to_read = first data unit size -");
    ret = read(fd, read_buf, 2);
    if (ret > 0)
        printf("PASSED\n");
    else
        printf("NOT PASSED\n");

    // TEST 7
    printf("TEST 7: len_to_read < first data unit size - ");
    ret = read(fd, read_buf, 2);
    if (ret < 0)
        printf("PASSED\n");
    else
        printf("NOT PASSED\n");

    // TEST 8
    printf("TEST 8: len_to_read = first data unit size - ");
    ret = read(fd, read_buf, 5);
    if (ret > 0)
        printf("PASSED\n");
    else
        printf("NOT PASSED\n");

    ioctl(fd, CHANGE_MAX_DATA_UNIT_SIZE_CTL, 128);

    close(fd);
    }
