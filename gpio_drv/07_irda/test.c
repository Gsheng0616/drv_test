
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
 * ./test /dev/myirda
 * release 1  释放
   release 0  按下
 *
 */
int main(int argc, char **argv)
{
	unsigned char buf[3];
	int ret;
	
	/* 1. 判断参数 */
	if (argc != 2) 
	{
		printf("Usage: %s <dev>\n", argv[0]);
		return -1;
	}


	/* 2. 打开文件 */
	fd = open(argv[1], O_RDWR);
	if (fd == -1)
	{
		printf("can not open file %s\n", argv[1]);
		return -1;
	}

	while (1)
	{
		if (read(fd, buf, 3) == 3)
			printf("addr %02x val %02x release %d\n", buf[0],buf[1],buf[2]);
		else
			printf("err: -1\n");
	}
	
	close(fd);
	
	return 0;
}


