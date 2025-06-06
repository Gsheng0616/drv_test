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

struct gpio_desc{
	int gpio;
	int irq;
    char name[128];
    int key;
	struct timer_list key_timer;
} ;

static struct gpio_desc *gpios;
static int count;

/* 主设备号                                                                 */
static int major = 0;
static struct class *gpio_class;

static int isr_cont = 0;
static u64 irda[68];

/* 环形缓冲区 */
#define BUF_LEN 128
static unsigned char g_keys[BUF_LEN];
static int r, w;

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

static void put_key(unsigned char key)
{
	if (!is_key_buf_full())
	{
		g_keys[w] = key;
		w = NEXT_POS(w);
	}
}

static unsigned char get_key(void)
{
	unsigned char key= 0;
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
	isr_cont = 0;
	
	put_key(1);
	put_key(1);

	wake_up_interruptible(&gpio_wait);
	kill_fasync(&button_fasync, SIGIO, POLL_IN);
}


/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t gpio_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	int err;
	static unsigned char last_irda_buf[3];
	unsigned char irda_buf[3];
	static int flags = 0;

	if (is_key_buf_empty() && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;
	
	wait_event_interruptible(gpio_wait, !is_key_buf_empty());
	
	irda_buf[0] = get_key();
	irda_buf[1] = get_key();
	irda_buf[2] = 0;

	if(irda_buf[0] == 0xff && irda_buf[1] == 0xff)
	{
		return -EINVAL;
	}

#if 1
	if(irda_buf[0] == 0 && irda_buf[1] == 0) //按下
	{
		irda_buf[0] = last_irda_buf[0];
		irda_buf[1] = last_irda_buf[1];
		irda_buf[2] = 0;
		flags = 1;
	}
	else if(irda_buf[0] == 1 && irda_buf[1] == 1) //松开
	{
		if(flags == 1)
		{
			del_timer(&gpios[0].key_timer);
			irda_buf[0] = last_irda_buf[0];
			irda_buf[1] = last_irda_buf[1];
			irda_buf[2] = 1;
			flags = 0;
		}
		else
		{
			irda_buf[0] = -1;
			irda_buf[1] = -1;
			irda_buf[2] = 1;
			printk("timer timeout!\n");
			return -EINVAL;
		}
	}
	else
	{
		last_irda_buf[0] = irda_buf[0];
		last_irda_buf[1] = irda_buf[1];
		flags = 1;
	}
#endif		
	err = copy_to_user(buf, irda_buf, 3);
	
	return 3;
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

void irda_para_datas(void)
{
	u64 time;
	int i;
	unsigned char datas = 0;
	int byte = 0;
	int bit = 0;
	unsigned char m,n;
	unsigned char buf_tmp[4];
	
	
	//解析引导码 9ms低 + 4.5高
	time = irda[1] - irda[0];
	if(time < 8*1000000 || time > 10*1000000)
	{
		goto err;
	}

	time = irda[2] - irda[1];
	if(time < 35*100000 || time > 55*100000)
	{
		goto err;
	}

	//解析数据
	for(i = 0;i<32;i++)
	{
		m = 3 + i*2;
		n = m + 1;
		time = irda[n] - irda[m];
		datas <<= 1;
		bit ++;
		if(time > 1*1000000)
		{
			datas |= 1;
		}

		if(bit == 8)
		{
			buf_tmp[byte] = datas;
			byte++;
			bit = 0;
			datas=0;
		}
	}

	buf_tmp[1] = ~buf_tmp[1];
	buf_tmp[3] = ~buf_tmp[3];
	if(buf_tmp[0] != buf_tmp[1] || buf_tmp[2] != buf_tmp[3])
	{
		printk("data verify err: %02x %02x %02x %02x\n", buf_tmp[0], buf_tmp[1], buf_tmp[2], buf_tmp[3]);
		goto err;
	}

	put_key(buf_tmp[0]);
	put_key(buf_tmp[2]);
	wake_up_interruptible(&gpio_wait);
	kill_fasync(&button_fasync, SIGIO, POLL_IN);

	return;
	
err:
	isr_cont = 0;
	put_key(-1);
	put_key(-1);
	wake_up_interruptible(&gpio_wait);
	kill_fasync(&button_fasync, SIGIO, POLL_IN);
}

int irda_para_redatas(void)
{	
	//解析引导码 9ms低 + 2.25高
	u64 time = irda[1] - irda[0];
	if(time < 8*1000000 || time > 10*1000000)
	{
		return -1;
	}

	time = irda[2] - irda[1];
	if(time < 2*1000000 || time > 25*100000)
	{
		return -1;
	}
	

	return 0;
}

static irqreturn_t gpio_key_isr(int irq, void *dev_id)
{
	u64 time;
	struct gpio_desc *gpio_desc = dev_id;
	//printk("gpio_key_isr key %d irq happened\n", gpio_desc->gpio);

	//计算时间，放进buf
	time = ktime_get_ns();
	irda[isr_cont] = time;
	
	//次数累加
	isr_cont++;

	//判断是否为重复码
	if(isr_cont == 4)
	{
		if(0 == irda_para_redatas())
		{
			put_key(0);
			put_key(0);
			isr_cont = 0;
			//del_timer(&gpio_desc->key_timer);
			wake_up_interruptible(&gpio_wait);
			kill_fasync(&button_fasync, SIGIO, POLL_IN);
			return IRQ_HANDLED;
		}		
	}

	
	//一个数据周期为64个中断 2n  (n为字节)	 +3 = 2*32 +3 +1 = 68 
	if(isr_cont == 68)
	{
		//del_timer(&gpio_desc->key_timer);
		irda_para_datas();
		isr_cont = 0;
		return IRQ_HANDLED;
	}

	//2.25*32+9.5+4.5 = 85.5
	mod_timer(&gpio_desc->key_timer, jiffies + msecs_to_jiffies(100));
	
	return IRQ_HANDLED;

}


/* 在入口函数 */
static int gpio_drv_probe(struct platform_device *pdev)
{
    int err = 0;
    int i;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
    int count = sizeof(gpios)/sizeof(gpios[0]);
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	

	/* 从platfrom_device获得引脚信息 
	 * 1. pdev来自c文件
     * 2. pdev来自设备树
	 */
	
	if (np)
	{
		/* pdev来自设备树 : 示例
        reg_usb_ltemodule: regulator@1 {
            compatible = "100ask,gpiodemo";
            gpios = <&gpio5 5 GPIO_ACTIVE_HIGH>, <&gpio5 3 GPIO_ACTIVE_HIGH>;
        };
		*/
		count = of_gpio_count(np);
		if (!count)
			return -EINVAL;

		gpios = kmalloc(count * sizeof(struct gpio_desc), GFP_KERNEL);
		for (i = 0; i < count; i++)
		{
			gpios[i].gpio = of_get_gpio(np, i);
			sprintf(gpios[i].name, "%s_pin_%d", np->name, i);
		}
	}
	else
	{
		/* pdev来自c文件 
		static struct resource omap16xx_gpio3_resources[] = {
			{
					.start  = 115,
					.end    = 115,
					.flags  = IORESOURCE_IRQ,
			},
			{
					.start  = 118,
					.end    = 118,
					.flags  = IORESOURCE_IRQ,
			},		};		
		*/
		count = 0;
		while (1)
		{
			res = platform_get_resource(pdev, IORESOURCE_IRQ, count);
			if (res)
			{
				count++;
			}
			else
			{
				break;
			}
		}

		if (!count)
			return -EINVAL;

		gpios = kmalloc(count * sizeof(struct gpio_desc), GFP_KERNEL);
		for (i = 0; i < count; i++)
		{
			res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
			gpios[i].gpio = res->start;
			sprintf(gpios[i].name, "%s_pin_%d", pdev->name, i);
		}

	}
	
	
	for (i = 0; i < count; i++)
	{		
		gpios[i].irq  = gpio_to_irq(gpios[i].gpio);

		setup_timer(&gpios[i].key_timer, key_timer_expire, (unsigned long)&gpios[i]);
		err = request_irq(gpios[i].irq, gpio_key_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, gpios[i].name, &gpios[i]);
	}

	/* 注册file_operations 	*/
	major = register_chrdev(0, "100ask_irda", &gpio_key_drv);  /* /dev/gpio_desc */

	gpio_class = class_create(THIS_MODULE, "100ask_irda_class");
	if (IS_ERR(gpio_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_irda");
		return PTR_ERR(gpio_class);
	}

	device_create(gpio_class, NULL, MKDEV(major, 0), NULL, "myirda"); /* /dev/100ask_gpio */
	

	
	return err;
}

/* 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数
 */
static int gpio_drv_remove(struct platform_device *pdev)
{
    int i;
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	device_destroy(gpio_class, MKDEV(major, 0));
	class_destroy(gpio_class);
	unregister_chrdev(major, "100ask_irda");

	for (i = 0; i < count; i++)
	{
		free_irq(gpios[i].irq, &gpios[i]);
		del_timer(&gpios[i].key_timer);
	}

	return 0;
}

static const struct of_device_id irda_dt_ids[] = {
        { .compatible = "100ask,irdagpio", },
        { /* sentinel */ }
};

static struct platform_driver gpio_platform_driver = {
	.driver		= {
		.name	= "100ask_irda_plat_drv",
		.of_match_table = irda_dt_ids,
	},
	.probe		= gpio_drv_probe,
	.remove		= gpio_drv_remove,
};

static int __init irda_drv_init(void)
{
	/* 注册platform_driver */
	return platform_driver_register(&gpio_platform_driver);
}

static void __exit irda_drv_exit(void)
{
	/* 反注册platform_driver */
	platform_driver_unregister(&gpio_platform_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(irda_drv_init);
module_exit(irda_drv_exit);

MODULE_LICENSE("GPL");


