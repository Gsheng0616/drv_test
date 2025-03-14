#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>


/*
./test /dev/xxx hello  写
./test /dev/xxx        读	
*/
int main(int argc, char **argv)
{
	int fd;
	int len;
	char buf[100];
	
	if(argc < 2)
	{
		printf("usage %s <dev> [string]",argv[0]);
		return -1;
	}
	
	fd = open(argv[1], O_RDWR);
	if(fd < 0 )
	{
		printf("open %s fail\n",argv[1]);
		return -1;
	}

	if(argc == 3)
	{
		len = write(fd, argv[2], strlen(argv[2])+1);
		printf("write ret = %d\n",len );
	}
	else
	{
		len = read(fd, buf, 100);
		buf[99] = '\n';
		printf("read %s ret = %d\n",buf,len );
	}

	return 0;
}
