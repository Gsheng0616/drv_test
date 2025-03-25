
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>

static int fd;
#define TRIG_CMD 100
/*
 * ./button_test /dev/100ask_button0
 *
 */
int main(int argc, char **argv)
{
	long int val;
	struct pollfd fds[1];
	int timeout_ms = 5000;
	int ret;
	int	flags;

	int i;
	
	/* 1. 判断参数 */
	if (argc != 2) 
	{
		printf("Usage: %s <dev>\n", argv[0]);
		return -1;
	}


	/* 2. 打开文件 */
	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (fd == -1)
	{
		printf("can not open file %s\n", argv[1]);
		return -1;
	}

	fds[0].fd = fd;
	fds[0].events = POLLIN;

	while(1)
	{
		ioctl(fd, TRIG_CMD);
		ret = poll(fds, 1, 5000);
		if(1 == ret)
		{
			if (read(fd, &val, 4) == 4)
				printf("get distance: %d cm\n", val*17/1000000);
			else
				printf("get distance: -1\n");
			sleep(1);
		}
		else
		{
			printf("get distance: err\n");
		}
	}


	close(fd);
	
	return 0;
}


