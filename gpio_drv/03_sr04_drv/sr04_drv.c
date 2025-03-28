﻿#include <linux/module.h>
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
#include <linux/delay.h> 
#include <linux/ktime.h> 

struct gpio_desc{
	int gpio;
	int irq;
    char *name;
    int key;
	struct timer_list key_timer;
} ;

static struct gpio_desc gpios[2] = {
    {115, 0, "sr04_trig", },
    {116, 0, "sr04_echo", },
};

/* 主设备号                                                                 */
static int major = 0;
static struct class *sr04_class;

/* 环形缓冲区 */
#define BUF_LEN 128
static int g_keys[BUF_LEN];
static int r, w;

#define TRIG_CMD 100
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

static void put_key(int key)
{
	if (!is_key_buf_full())
	{
		g_keys[w] = key;
		w = NEXT_POS(w);
	}
}

static int get_key(void)
{
	int key = 0;
	if (!is_key_buf_empty())
	{
		key = g_keys[r];
		r = NEXT_POS(r);
	}
	return key;
}


static DECLARE_WAIT_QUEUE_HEAD(gpio_wait);

/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t gpio_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	int err;
	int key;

	if (is_key_buf_empty() && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;
	
	wait_event_interruptible(gpio_wait, !is_key_buf_empty());
	key = get_key();
	if(key == -1)
		return -ENODATA;
	err = copy_to_user(buf, &key, 4);
	
	return 4;
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

static long ioctl_trig(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd)
	{
		case TRIG_CMD:
		{
			gpio_set_value(gpios[0].gpio, 1);
			udelay(20);
			gpio_set_value(gpios[0].gpio, 0);

			//启动定时器
			mod_timer(&gpios[1].key_timer, jiffies + msecs_to_jiffies(50));
		}
	}


	return 0;
}

static void sr04_timer_func(unsigned long data)
{
		//唤醒读程序
		put_key(-1);
		wake_up_interruptible(&gpio_wait);
		kill_fasync(&button_fasync, SIGIO, POLL_IN);

}
/* 定义自己的file_operations结构体                                              */
static struct file_operations gpio_key_drv = {
	.owner	 = THIS_MODULE,
	.read    = gpio_drv_read,
	.poll    = gpio_drv_poll,
	.fasync  = gpio_drv_fasync,
	.unlocked_ioctl = ioctl_trig,
};


static irqreturn_t gpio_sr04_isr(int irq, void *dev_id)
{
	struct gpio_desc *gpio_desc = dev_id;
	int val;
	static u64 rising_time = 0;

	//printk("gpio_key_isr key %d irq happened\n", gpio_desc->gpio);

	val = gpio_get_value(gpio_desc->gpio);

	if(val)
	{
		//上升沿
		rising_time = ktime_get_ns();
	}
	else
	{
		if(rising_time == 0)
		{
			return IRQ_HANDLED;
		}
		rising_time = ktime_get_ns() - rising_time;
	
		//删除定时器
		del_timer(&gpios[1].key_timer);
		
		put_key(rising_time);
		rising_time = 0;
		wake_up_interruptible(&gpio_wait);
		kill_fasync(&button_fasync, SIGIO, POLL_IN);

	}

	
	return IRQ_HANDLED;
}


/* 在入口函数 */
static int __init gpio_drv_init(void)
{
    int err;
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	gpio_request(gpios[0].gpio, gpios[0].name);

	gpio_direction_output(gpios[0].gpio,0);

	

	
	gpios[1].irq  = gpio_to_irq(gpios[1].gpio);

	err = request_irq(gpios[1].irq, gpio_sr04_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, gpios[1].name, &gpios[1]);

	//设置定时器
	setup_timer(&gpios[1].key_timer, sr04_timer_func, (unsigned long)&gpios[1]);

	/* 注册file_operations 	*/
	major = register_chrdev(0, "100ask_sr04", &gpio_key_drv);  /* /dev/gpio_desc */

	sr04_class = class_create(THIS_MODULE, "100ask_sr04_class");
	if (IS_ERR(sr04_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_sr04_class");
		return PTR_ERR(sr04_class);
	}

	device_create(sr04_class, NULL, MKDEV(major, 0), NULL, "100ask_sr04"); /* /dev/100ask_gpio */
	
	return err;
}

/* 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数
 */
static void __exit gpio_drv_exit(void)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	device_destroy(sr04_class, MKDEV(major, 0));
	class_destroy(sr04_class);
	unregister_chrdev(major, "100ask_sr04");


	gpio_free(gpios[0].gpio);
	free_irq(gpios[1].irq, &gpios[1]);
	del_timer(&gpios[1].key_timer);
}


/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(gpio_drv_init);
module_exit(gpio_drv_exit);

MODULE_LICENSE("GPL");


