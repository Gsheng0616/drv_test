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
#include <linux/fb.h>
#include <sys/mman.h>


int oled_width = 128;  // OLED 屏幕宽度
int oled_height = 64;  // OLED 屏幕高度
static unsigned int line_width;
static unsigned int pixel_width;
int fd;

// 全局变量存储当前IP（线程共享）
pthread_mutex_t ip_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t fbp_mutex = PTHREAD_MUTEX_INITIALIZER;

char current_ip[INET_ADDRSTRLEN] = "0.0.0.0";


struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
char *fbp = 0;
int fbfd = 0;

// 清屏函数
void fb_clear()
{
	pthread_mutex_lock(&fbp_mutex);
	memset(fbp ,0x00, finfo.smem_len);
	pthread_mutex_unlock(&fbp_mutex);
}


// 初始化framebuffer
int fb_init(char* buf)
{
    // 打开设备
    fbfd = open(buf, O_RDWR);
    if (fbfd == -1) {
        perror("open fb device");
        return -1;
    }

    // 获取固定屏幕信息
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        perror("get fixed info");
        return -1;
    }

    // 获取可变屏幕信息
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("get variable info");
        return -1;
    }

	line_width = finfo.line_length;   			//xres*bpp/8  算出一行多少字节
	pixel_width = vinfo.bits_per_pixel / 8;		//算出1个像素占几个字节s
		
    // 映射framebuffer到内存
    fbp = (char *)mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == MAP_FAILED) {
        perror("mmap fb");
        return -1;
    }

	fb_clear();  //清屏
	
    return 0;
}


void oled_set_pixel(int x, int y, int color) {
    // 参数检查
    if (x < 0 || x >= oled_width || y < 0 || y >= oled_height) return;

    // 计算字节和位偏移（MSB优先，即bit7对应最左像素）
    int byte_offset = (y * (oled_width / 8)) + (x / 8);
    int bit_offset  = (x % 8);  // 调整为硬件位序（可能需要根据OLED调整）

    pthread_mutex_lock(&fbp_mutex);
    // 设置或清除指定位
    if (color) {
        fbp[byte_offset] |=  (1 << bit_offset);   // 点亮像素
    } else {
        fbp[byte_offset] &= ~(1 << bit_offset);   // 熄灭像素
    }
    pthread_mutex_unlock(&fbp_mutex);

}

void oled_draw_char(int x, int y, unsigned char c) 
{
	const unsigned char *dots = &fontdata_8x16[c*16];

    // 字符尺寸（8x16）
    const int char_width = 8;
    const int char_height = 16;

    for (int dy = 0; dy < char_height; dy++) //一个字符有16个字节
	{
        uint8_t font_byte = dots[dy];  // 字体数据的一行（8位）

        // 计算显存目标行地址  点的绝对坐标计算方式,算出在哪个字节：y*xres*bpp/8  +  x*bpp/8
        int row_offset = (y + dy)*finfo.line_length;  // 计算行坐标，一行16字节
        int byte_pos = row_offset + (x*vinfo.bits_per_pixel / 8);    // 目标字节位置
        int bit_offset = x % 8;                 // 起始位偏移

        // 处理当前行字体数据的8位
        for (int dx = 0; dx < char_width; dx++) 
        {
            // 取出字体数据的当前位（MSB优先 即bit7对应最左像素）
            int bit = (font_byte >> (7 - dx)) & 0x01;


            // 设置显存中的对应位
            if (bit) {
                fbp[byte_pos] |= (1 << (7 - bit_offset));
            } else {
                fbp[byte_pos] &= ~(1 << (7 - bit_offset));
            }

            // 移动到下一个位（可能跨字节）
            bit_offset++;
            if (bit_offset >= 8) {
                bit_offset = 0;
                byte_pos++;
            }
        }
    }
}



void OLED_DIsp_String(int x, int y, const char *str, int char_width, int char_height) {        

	if((x < 0 || x > oled_width - 1) || (y < 0 || y > oled_height - 1)) //width 127 height 63
	{
		printf("x y value eroor!");
		return;
	}
	
    while (*str != '\0') 
	{  
        oled_draw_char(x, y, *str++);
        x += char_width;
        if (x > oled_width - 1) {
            x = 0;
            y += char_height;
        }
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
			fb_clear();
			OLED_DIsp_String(50, 20, "ip:", 8, 16);
			OLED_DIsp_String(20, 36, current_ip, 8, 16);
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
	fb_init(argv[1]);
	
	// 主线程模拟其他任务（例如显示IP）
   while (1) 
   {
		pthread_mutex_lock(&ip_mutex);
		printf("Current IP: %s\n", current_ip);
		pthread_mutex_unlock(&ip_mutex);
		sleep(3); // 主线程每秒打印一次
    }

	close(fd);
	
	return 0;
}



