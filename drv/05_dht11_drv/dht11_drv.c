#include <linux/module.h>
#include <linux/poll.h>
#include <linux/delay.h>
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
    {115, 0, "dht11", },
};

/* 主设备号                                                                 */
static int major = 0;
static struct class *gpio_class;

/* 环形缓冲区 */
#define BUF_LEN 128
static char g_keys[BUF_LEN];
static int r, w;


static u64 g_dht11_irq_time[84];
static int g_dht11_irq_cnt = 0;



static irqreturn_t dht11_isr(int irq, void *dev_id);
void  parse_dht11_data(void);

struct fasync_struct *button_fasync;

#define NEXT_POS(x) ((x+1) % BUF_LEN)

static int is_key_buf_empty(void)
{
	return (r == w);
}

static int is_key_buf_full(void)
{
	return (r == NEXT_POS(w));
}

static void put_key(char key)
{
	if (!is_key_buf_full())
	{
		g_keys[w] = key;
		w = NEXT_POS(w);
	}
}

static char get_key(void)
{
	char key = 0;
	if (!is_key_buf_empty())
	{
		key = g_keys[r];
		r = NEXT_POS(r);
	}
	return key;
}


static DECLARE_WAIT_QUEUE_HEAD(gpio_wait);
// static void key_timer_expire(struct timer_list *t)
static void key_timer_expire(unsigned long data)
{
	parse_dht11_data();
}

/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t gpio_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	int err;
	char temp_buf[2];

	if(size != 2)
		return -EINVAL;

	/* 1. 发送18ms的低脉冲 */
	gpio_request(gpios[0].gpio, gpios[0].name);
	gpio_direction_output(gpios[0].gpio, 0);
	mdelay(18);
	gpio_free(gpios[0].gpio);
	

	//修改为输入
	gpio_direction_input(gpios[0].gpio);
	err = request_irq(gpios[0].irq, dht11_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, gpios[0].name, &gpios[0]);

	//启动定时器，防止硬件错误，一直无返回
	mod_timer(&gpios[0].key_timer, jiffies + 10);//10个滴答
	
	
	wait_event_interruptible(gpio_wait, !is_key_buf_empty());
	free_irq(gpios[0].irq,&gpios[0]);

	//恢复GPIO引脚
	gpio_request(gpios[0].gpio, gpios[0].name);
	gpio_direction_output(gpios[0].gpio, 1);
	gpio_free(gpios[0].gpio);
	
	temp_buf[0] = get_key();
	temp_buf[1] = get_key();
	
	if(temp_buf[0] == 0xff && temp_buf[1] == 0xff)
	{
		return -EIO;
	}
	printk("%d %d \n",temp_buf[0], temp_buf[1]);
	err = copy_to_user(buf, temp_buf, size);

	return size;
}


static unsigned int gpio_drv_poll(struct file *fp, poll_table * wait)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	poll_wait(fp, &gpio_wait, wait);
	return is_key_buf_empty() ? 0 : POLLIN | POLLRDNORM;
}

static int gpio_drv_fasync(int fd, struct file *file, int on)
{
	if (fasync_helper(fd, file, on, &button_fasync) >= 0)
		return 0;
	else
		return -EIO;
}


/* 定义自己的file_operations结构体                                              */
static struct file_operations gpio_key_drv = {
	.owner	 = THIS_MODULE,
	.read    = gpio_drv_read,
	.poll    = gpio_drv_poll,
	.fasync  = gpio_drv_fasync,
};

void  parse_dht11_data(void)
{
	int i;
	u64 rising_time;
	//u64 losing_time;
	unsigned char datas[5];
	unsigned char data = 0;
	int byte = 0;
	int bit = 0;
	unsigned char crc = 0;

	if(g_dht11_irq_cnt < 81)
	{
		printk("parse_dht11_data err %d!" ,g_dht11_irq_cnt);
		put_key(-1);
		put_key(-1);
		wake_up_interruptible(&gpio_wait);
		g_dht11_irq_cnt = 0;
		return;
	}
	
	//81 82 83 84个中断
	for(i = g_dht11_irq_cnt - 80; i< g_dht11_irq_cnt;i+=2)
	{
		rising_time = g_dht11_irq_time[i] - g_dht11_irq_time[i-1];
		//losing_time = g_dht11_irq_time[i-1] - g_dht11_irq_time[i-2];

		//1
		data <<= 1;
		if(rising_time > 50000)
		{
			data |= 1;
		}
		bit ++;

		if(bit == 8) //一个字节数据
		{
			datas[byte] = data;
			data = 0;
			bit = 0;
			byte ++;
		}
		
	}

	//crc校验 放入环形缓冲区
	crc = (datas[0] + datas[1] + datas[2] + datas[3]);
	if(crc == datas[4])
	{
		put_key(datas[0]);
		put_key(datas[2]);
	}
	else
	{
		put_key(-1);
		put_key(-1);
	}

	g_dht11_irq_cnt = 0;
	wake_up_interruptible(&gpio_wait);
	
}

static irqreturn_t dht11_isr(int irq, void *dev_id)
{
	u64 time;
	struct gpio_desc *gpio_desc = dev_id;
	//printk("gpio_key_isr key %d irq happened\n", gpio_desc->gpio);
	//mod_timer(&gpio_desc->key_timer, jiffies + HZ/5);

	time = ktime_get_ns();
	g_dht11_irq_time[g_dht11_irq_cnt] = time;
	g_dht11_irq_cnt ++;
	if(g_dht11_irq_cnt == 84)
	{
		del_timer(&gpio_desc->key_timer);
		parse_dht11_data();
	}

	return IRQ_HANDLED;
}


/* 在入口函数 */
static int __init gpio_drv_init(void)
{
    int err = 0;
    int i;
    int count = sizeof(gpios)/sizeof(gpios[0]);
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	
	for (i = 0; i < count; i++)
	{	
		//获得引脚中断号
		gpios[i].irq  = gpio_to_irq(gpios[i].gpio);

		//设置GPIO初始值
		gpio_request(gpios[i].gpio, gpios[i].name);
		gpio_direction_output(gpios[i].gpio, 1);
		gpio_free(gpios[i].gpio);

		//设置定时器
		setup_timer(&gpios[i].key_timer, key_timer_expire, (unsigned long)&gpios[i]);
	 	//timer_setup(&gpios[i].key_timer, key_timer_expire, 0);
		//gpios[i].key_timer.expires = ~0;
		//add_timer(&gpios[i].key_timer);
		//err = request_irq(gpios[i].irq, gpio_key_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "100ask_gpio_key", &gpios[i]);
	}

	/* 注册file_operations 	*/
	major = register_chrdev(0, "100ask_dht11", &gpio_key_drv);  /* /dev/gpio_desc */

	gpio_class = class_create(THIS_MODULE, "100ask_dht11_class");
	if (IS_ERR(gpio_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_dht11");
		return PTR_ERR(gpio_class);
	}

	device_create(gpio_class, NULL, MKDEV(major, 0), NULL, "mydht11"); /* /dev/100ask_gpio */
	
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
	unregister_chrdev(major, "100ask_dht11");

	for (i = 0; i < count; i++)
	{
		//free_irq(gpios[i].irq, &gpios[i]);
		//del_timer(&gpios[i].key_timer);
	}
}


/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(gpio_drv_init);
module_exit(gpio_drv_exit);

MODULE_LICENSE("GPL");


