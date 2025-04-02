#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "font.h"
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>


static int fd;
#define OLED_IOC_INIT 			123
#define OLED_IOC_SET_POS 		124

int oled_width = 128;  // OLED 屏幕宽度
int oled_height = 64;  // OLED 屏幕高度

// 全局变量存储当前IP（线程共享）
pthread_mutex_t ip_mutex = PTHREAD_MUTEX_INITIALIZER;
char current_ip[INET_ADDRSTRLEN] = "0.0.0.0";



void OLED_DIsp_Set_Pos(int x, int y)
{
	ioctl(fd, OLED_IOC_SET_POS, x | (y<<8));
}


void oled_write_datas(const unsigned char *buf, int len)
{
	write(fd, buf, len);		
}

/**
 * @brief 在 OLED 上显示一个 8x16 像素的 ASCII 字符
 * @param x 起始 X 坐标（0~127）
 * @param y 起始 Y 坐标（0~7，每行 8 像素）
 * @param c ASCII 字符（范围：' ' ~ 127）
 */
void OLED_DIsp_Char(int x, int y, unsigned char c) {
    /* 字符范围检查 */
    if (c < ' ' || c > 127) c = ' ';  // 非法字符替换为空格

    /* 坐标边界检查 */
    if (x < 0 || x > oled_width - 1 || y < 0 || y > oled_height / 8 - 1) {
        return;
    }

    /* 获取字模数据（16 字节） */
    const unsigned char *dots = oled_asc2_8x16[c - ' '];

    /* 发送上半部分 8 字节 */
    OLED_DIsp_Set_Pos(x, y);
    oled_write_datas(&dots[0], 8);

    /* 发送下半部分 8 字节 */
    OLED_DIsp_Set_Pos(x, y + 1);
    oled_write_datas(&dots[8], 8);
}


/**
 * @brief 在 OLED 上显示字符串，自动换行
 * @param x 起始 X 坐标（像素）
 * @param y 起始 Y 坐标（像素）
 * @param str 要显示的字符串（必须以 \0 结尾）
 * @param char_width 单个字符宽度（像素）
 * @param line_height 行高（像素）
 */
void OLED_DIsp_String(int x, int y, const char *str, int char_width, int line_height) {        

    while (*str != '\0' && y < oled_height/8) {
        OLED_DIsp_Char(x, y, *str++);
        x += char_width;
        if (x > oled_width - 1) {
            x = 0;
            y += line_height/8;
        }
    }
}


void OLED_DIsp_Clear(void)  
{
    unsigned char x, y;
	char buf[128];

	memset(buf, 0x00, sizeof(buf));
	
    for (y = 0; y < 8; y++)
    {
    	OLED_DIsp_Set_Pos(x,y);
		oled_write_datas(buf,sizeof(buf));	

    }
	
}

void get_local_ip(char *ip_buffer, const char *interface) {
    struct ifaddrs *ifaddr, *ifa;
    getifaddrs(&ifaddr);  // 获取所有网络接口

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) 
            continue;  // 跳过非IPv4接口

        if (!interface || strcmp(ifa->ifa_name, interface) == 0) {
            getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                      ip_buffer, INET_ADDRSTRLEN, NULL, 0, NI_NUMERICHOST);
            break;
        }
    }
    freeifaddrs(ifaddr);  // 释放接口链表
}



// 线程函数：定期更新IP
void *update_ip_thread(void *arg) {
    const char *interface = (const char *)arg; // 网卡名称（如"eth0"）
    char new_ip[INET_ADDRSTRLEN];
	
    // 设置为分离线程（线程退出时自动释放资源）
    pthread_detach(pthread_self());

    while (1) {
        get_local_ip(new_ip, interface);

        // 加锁保护全局变量
        pthread_mutex_lock(&ip_mutex);
        if (strcmp(current_ip, new_ip) != 0) {
            strncpy(current_ip, new_ip, INET_ADDRSTRLEN);
			OLED_DIsp_Clear();
			OLED_DIsp_String(50, 3, "ip:", 8, 16);
			OLED_DIsp_String(3, 5, current_ip, 8, 16);
            //printf("IP Updated: %s (Interface: %s)\n", current_ip, interface);
        }
        pthread_mutex_unlock(&ip_mutex);

        sleep(5); // 每5秒检查一次
    }

    return NULL;
}


int create_update_ip_thread_func()
{
	pthread_t tid;
	const char *interface = "eth0"; // 指定网卡（可改为"wlan0"）
	
	// 创建线程
    if (pthread_create(&tid, NULL, update_ip_thread, (void *)interface) != 0) {
        perror("pthread_create");
        return -1;
    }
	
	return 0;
}
/*
 * ./button_test /dev/100ask_button0
 *
 */
int main(int argc, char **argv)
{
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

	if(create_update_ip_thread_func()!=0)
	{
		printf("create_update_ip_thread_func fail!");
		return -1;
	}

	ioctl(fd, OLED_IOC_INIT);

	// 主线程模拟其他任务（例如显示IP）
    while (1) {
        pthread_mutex_lock(&ip_mutex);
        printf("Current IP: %s\n", current_ip);
        pthread_mutex_unlock(&ip_mutex);
        sleep(1); // 主线程每秒打印一次
    }

	close(fd);
	
	return 0;
}


