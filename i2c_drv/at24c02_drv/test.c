
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
 * ./test /dev/myat24c02 string 写 
 * ./test /dev/myat24c02  读
 */
int main(int argc, char **argv)
{
	char buf[100];

	
	/* 1. 判断参数 */
	if (argc < 2) 
	{
		printf("Usage: %s <dev> [string]\n", argv[0]);
		return -1;
	}


	/* 2. 打开文件 */
	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (fd == -1)
	{
		printf("can not open file %s\n", argv[1]);
		return -1;
	}

	if(argc == 2) //读
	{
		read(fd, buf, 100);
		printf("value %s\n", buf);
	}
	else
	{	
		write(fd, argv[2], strlen(argv[2])+1);
	}
		
	
	close(fd);
	
	return 0;
}


