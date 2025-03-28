
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <linux/input.h>

static int fd;

/*
 * ./button_test /dev/100ask_button0
 *
 */
int main(int argc, char **argv)
{
	unsigned char buf[3];
	struct input_event event;
	int ret;
	struct pollfd fds[1];
	int timeout_ms = 5000;
	
	/* 1. 判断参数 */
	if (argc != 2) 
	{
		printf("Usage: %s <dev>\n", argv[0]);
		return -1;
	}


	/* 2. 打开文件 */
	fd = open(argv[1], O_RDWR| O_NONBLOCK);
	if (fd == -1)
	{
		printf("can not open file %s\n", argv[1]);
		return -1;
	}

	fds[0].fd = fd;
	fds[0].events = POLLIN;


	while (1)
	{
		ret = poll(fds, 1, 5000);
		if(1 == ret)
		{
			ret = read(fd, &event, sizeof(struct input_event));
			if (ret == sizeof(struct input_event))
			{
				printf("type %x, code %x, value %d\n", event.type, event.code, event.value);
			}
			else
			{
				printf("ret: %d\n",ret);
			}
		}
		else if (0 == ret)
		{
			printf("timeout!\n");
		}
		else
		{
			printf("get distance: err\n");
		}
	}
	
	close(fd);
	
	return 0;
}


