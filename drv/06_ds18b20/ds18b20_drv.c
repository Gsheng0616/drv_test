#include <linux/module.h>
#include <linux/poll.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/timer.h>

struct gpio_desc{
	int gpio;
	int irq;
    char *name;
    int key;
	struct timer_list key_timer;
} ;

static struct gpio_desc gpios[] = {
    {115, 0, "ds18b20", },
};

/* 主设备号                                                                 */
static int major = 0;
static struct class *gpio_class;

static spinlock_t ds18b20_lock;



void delay_us(int us)
{
	u64 time = ktime_get_ns();
	while(ktime_get_ns() - time < us *1000);
}

int send_reset_wait_ack(void)
{
	int count = 100;
	gpio_set_value(gpios[0].gpio, 0);
	delay_us(480);

	gpio_direction_input(gpios[0].gpio);
	while(gpio_get_value(gpios[0].gpio) && count--)
	{
		delay_us(1);
	}
	if(count == 0)
	{
		printk("ds18b20 no ack!\n");
		return -EIO;
	}

	/* 等待ACK结束 */
	count = 300;
	while(!gpio_get_value(gpios[0].gpio) && count--)
	{
		delay_us(1);
	}
	if(count == 0)
	{
		printk("ds18b20 no ack!\n");
		return -EIO;
	}

	return 0;
}

void ds18b20_write_byte(unsigned char cmd)
{
	int i;
	gpio_direction_output(gpios[0].gpio, 1);
	for(i =0 ; i< 8; i++)
	{
		if(cmd & 1 << i)
		{
			gpio_direction_output(gpios[0].gpio, 0);
			delay_us(2);
			gpio_direction_output(gpios[0].gpio, 1);
			delay_us(60);
		}
		else
		{
			gpio_direction_output(gpios[0].gpio, 0);
			delay_us(60);
			gpio_direction_output(gpios[0].gpio, 1);
		}
	}
}

void ds18b20_read_byte(unsigned char *buf)
{
	unsigned char data = 0;
	int i;
	
	gpio_direction_output(gpios[0].gpio, 1);
	for(i = 0 ; i<8 ;i++)
	{
		gpio_direction_output(gpios[0].gpio, 0);
		delay_us(2);
		gpio_direction_input(gpios[0].gpio);
		delay_us(15);
		
		while(gpio_get_value(gpios[0].gpio))
		{
			data |= (1<<i); 
		}		
		delay_us(50);
		gpio_direction_output(gpios[0].gpio, 1);
		
	}
	buf[0] = data;
}

static unsigned char calcrc_1byte(unsigned char abyte)   
{   
	unsigned char i,crc_1byte;     
	crc_1byte=0;                //设定crc_1byte初值为0  
	for(i = 0; i < 8; i++)   
	{   
		if(((crc_1byte^abyte)&0x01))   
		{   
			crc_1byte^=0x18;     
			crc_1byte>>=1;   
			crc_1byte|=0x80;   
		}         
		else     
			crc_1byte>>=1;   

		abyte>>=1;         
	}   
	return crc_1byte;   
}

/* 参考: https://www.cnblogs.com/yuanguanghui/p/12737740.html */   
static unsigned char calcrc_bytes(unsigned char *p,unsigned char len)  
{  
	unsigned char crc=0;  
	while(len--) //len为总共要校验的字节数  
	{  
		crc=calcrc_1byte(crc^*p++);  
	}  
	return crc;  //若最终返回的crc为0，则数据传输正确  
}  

int crc_verifite_func(unsigned char buf[])
{
	unsigned char crc;

	crc = calcrc_bytes(buf, 8);

    if (crc == buf[8])
		return 0;
	else
		return -1;
}

void ds18b20_convert_func(unsigned char src_buf[] , unsigned int dst_buf[])
{
	unsigned char tempL=0,tempH=0;
	unsigned int integer;
	unsigned char decimal1,decimal2,decimal;

	tempL = src_buf[0]; //读温度低8位
	tempH = src_buf[1]; //读温度高8位

	if (tempH > 0x7f)      							//最高位为1时温度是负
	{
		tempL    = ~tempL;         				    //补码转换，取反加一
		tempH    = ~tempH+1;      
		integer  = tempL/16+tempH*16;      			//整数部分
		decimal1 = (tempL&0x0f)*10/16; 			//小数第一位
		decimal2 = (tempL&0x0f)*100/16%10;			//小数第二位
		decimal  = decimal1*10+decimal2; 			//小数两位
	}
	else
	{
		integer  = tempL/16+tempH*16;      				//整数部分
		decimal1 = (tempL&0x0f)*10/16; 					//小数第一位
		decimal2 = (tempL&0x0f)*100/16%10;				//小数第二位
		decimal  = decimal1*10+decimal2; 				//小数两位
	}
	dst_buf[0] = integer;
	dst_buf[1] = decimal;
}

/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t gpio_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	int ret;
	unsigned long flags;
	int i;
	unsigned char kernel_buf[9];
	unsigned int data[2];

	if(size != 8 )
	{
		printk("size != 8\n");
		return -EINVAL;
	}
	//关中断
	spin_lock_irqsave(&ds18b20_lock, flags);
	
	//发出复位信号并等回应信号
	ret = send_reset_wait_ack();
	if(ret)
	{
		spin_unlock_irqrestore(&ds18b20_lock, flags);
		return -ret;
	}
	
	//发送cc命令，忽略ROM地址
	ds18b20_write_byte(0xcc);
	//发送44h，开启温度转换
	ds18b20_write_byte(0x44);
	//恢复中断
	spin_unlock_irqrestore(&ds18b20_lock, flags);
	
	//等待温度转换完成 1s
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(1000));

	//读取9个字节数据

	//关中断
	spin_lock_irqsave(&ds18b20_lock, flags);
	//发出复位信号并等回应信号
	ret = send_reset_wait_ack();
	{
		spin_unlock_irqrestore(&ds18b20_lock, flags);
		return -ret;
	}


	//发送cc命令，忽略ROM地址
	ds18b20_write_byte(0xcc);
	//发送44h，开启温度转换
	ds18b20_write_byte(0xbe);

	
	//读取数据
	for(i = 0 ;i <9 ;i++)
	{
		ds18b20_read_byte(&kernel_buf[i]);
	}

	//恢复中断
	spin_unlock_irqrestore(&ds18b20_lock, flags);
	

	//CRC校验
	if(crc_verifite_func(kernel_buf))
	{
		printk("crc fail!\n");
		return -1;
	}
	
	//取出整数与小数
	ds18b20_convert_func(kernel_buf , data);
	
	ret = copy_to_user(buf, data, 8);
	printk("crc fail!\n");
	return 8;
}


/* 定义自己的file_operations结构体                                              */
static struct file_operations gpio_key_drv = {
	.owner	 = THIS_MODULE,
	.read    = gpio_drv_read,
};



/* 在入口函数 */
static int __init gpio_drv_init(void)
{
    int err = 0;
    int i;
    int count = sizeof(gpios)/sizeof(gpios[0]);
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	
	for (i = 0; i < count; i++)
	{		
		gpio_request(gpios[i].gpio, gpios[i].name);
		gpio_direction_output(gpios[i].gpio, 1);
	}
	//初始化
	spin_lock_init(&ds18b20_lock);
	
	/* 注册file_operations 	*/
	major = register_chrdev(0, "100ask_ds18b20", &gpio_key_drv);  /* /dev/gpio_desc */

	gpio_class = class_create(THIS_MODULE, "100ask_ds18b20_class");
	if (IS_ERR(gpio_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_ds18b20");
		return PTR_ERR(gpio_class);
	}

	device_create(gpio_class, NULL, MKDEV(major, 0), NULL, "ds18b20"); /* /dev/100ask_gpio */
	
	return err;
}

/* 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数
 */
static void __exit gpio_drv_exit(void)
{
    int i;
    int count = sizeof(gpios)/sizeof(gpios[0]);
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	device_destroy(gpio_class, MKDEV(major, 0));
	class_destroy(gpio_class);
	unregister_chrdev(major, "100ask_ds18b20");

	for (i = 0; i < count; i++)
	{
		gpio_free(gpios[i].gpio);
	}
}


/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(gpio_drv_init);
module_exit(gpio_drv_exit);

MODULE_LICENSE("GPL");


