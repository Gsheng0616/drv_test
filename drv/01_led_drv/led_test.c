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
 * ./led_test 1|2|3|...|  on|off
 *
 */
int main(int argc, char **argv)
{
	int ret;
	char buf[2];
	
	/* 1. 判断参数 */
	if (argc < 2) 
	{
		printf("Usage: %s <1|2|3|...|> [on|off]\n", argv[0]);
		return -1;
	}


	/* 2. 打开文件 */
	fd = open("/dev/100ask_led", O_RDWR | O_NONBLOCK);
	if (fd == -1)
	{
		printf("can not open file %s\n", "/dev/100ask_led");
		return -1;
	}

	buf[0] = strtol(argv[1],NULL,0);
	if(argc == 3)
	{
		if(strcmp(argv[2], "on") == 0)
		{
			buf[1] = 0;
		}
		else
			buf[1] = 1;

		ret = write(fd, buf, sizeof(buf)/sizeof(buf[0]));
		printf("gpio %d %d\n", buf[0],buf[1]);
	}
	else
	{
		ret = read(fd, buf, sizeof(buf)/sizeof(buf[0]));
		if(ret == 2)
		{
			printf("gpio %d %s\n",buf[0],buf[1] == 1 ? "off" : "on");
		}
	}
	
	close(fd);
	
	return 0;
}


