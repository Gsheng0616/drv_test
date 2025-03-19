#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <signal.h>

static int fd;

/*
 * ./motor_test /dev/100ask_motor 步数 速度
 *
 */
int main(int argc, char **argv)
{
	int val;
	struct pollfd fds[1];
	int timeout_ms = 5000;
	int ret;
	int	flags;
	int buf[2];
	int i;
	
	/* 1. 判断参数 */
	if (argc != 4) 
	{
		printf("Usage: %s <dev> <步数方向> <速度>\n", argv[0]);
		return -1;
	}


	/* 2. 打开文件 */
	fd = open(argv[1], O_RDWR);
	if (fd == -1)
	{
		printf("can not open file %s\n", argv[1]);
		return -1;
	}

	buf[0] = strtol(argv[2] , NULL , 0);
	buf[1] = strtol(argv[3] , NULL , 0);

	printf("step %d  speed %d\n", buf[0] , buf[1]);
	
	write(fd , buf , 8);
	
	close(fd);
	
	return 0;
}


