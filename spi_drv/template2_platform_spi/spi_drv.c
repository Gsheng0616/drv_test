﻿#include <linux/module.h>
#include <linux/poll.h>
#include <linux/spi/spi.h> 
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


//static struct gpio_desc *spi_gpios;
static struct spi_device *g_spi_device;


/* 主设备号                                                                 */
static int major = 0;
static struct class *spi_class;


struct fasync_struct *button_fasync;

static DECLARE_WAIT_QUEUE_HEAD(gpio_wait);




/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t spi_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	int err;
	struct spi_message message;
	//struct spi_transfer xfer[2];
	unsigned char kerel_buf[100];

	/* 通过spi控制器传输数据*/
	err = spi_sync(g_spi_device, &message);

	
	/*拷贝回用户空间*/
	err = copy_to_user(buf, kerel_buf, size);
	
	return size;
}

static ssize_t spi_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
	int err;
	struct spi_message message;
	//struct spi_transfer xfer[2];
	unsigned char kerel_buf[100];

	/*从用户空间拷贝数据*/
	err = copy_from_user(kerel_buf, buf, size);
	
	/*通过spi控制器传输数据*/
    err = spi_sync(g_spi_device, &message);

    
    return size;    
}


static unsigned int spi_drv_poll(struct file *fp, poll_table * wait)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	poll_wait(fp, &gpio_wait, wait);
	//return is_key_buf_empty() ? 0 : POLLIN | POLLRDNORM;
	return 0;
}

static int spi_drv_fasync(int fd, struct file *file, int on)
{
	if (fasync_helper(fd, file, on, &button_fasync) >= 0)
		return 0;
	else
		return -EIO;
}


/* 定义自己的file_operations结构体                                              */
static struct file_operations spi_drv = {
	.owner	 = THIS_MODULE,
	.read    = spi_drv_read,
	.write   = spi_drv_write,
	.poll    = spi_drv_poll,
	.fasync  = spi_drv_fasync,
};


/* 在入口函数 */
static int spi_drv_probe(struct spi_device *spi)
{
	//获取节点信息
	g_spi_device = spi;



	//注册字符驱动程序
	/* 注册file_operations 	*/
	major = register_chrdev(0, "100ask_spi", &spi_drv);  /* /dev/gpio_desc */

	spi_class = class_create(THIS_MODULE, "100ask_spi_class");
	if (IS_ERR(spi_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_spi");
		return PTR_ERR(spi_class);
	}

	return 0;
}



/* 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数 */
static int spi_drv_remove(struct spi_device *spi)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	device_destroy(spi_class, MKDEV(major, 0));
	class_destroy(spi_class);
	unregister_chrdev(major, "100ask_spi");


	return 0;
}

static const struct of_device_id spi_dt_ids[] = {
        { .compatible = "100ask,devspi", },
        { /* sentinel */ }
};

static struct spi_driver spi_driver = {
	.driver = {
		.name = "spi_drv",
		.of_match_table = spi_dt_ids,
	},
	.probe  = spi_drv_probe,
	.remove = spi_drv_remove,
};


static int __init spi_drv_init(void)
{
	/* 注册spi_driver */
	return spi_register_driver(&spi_driver);
}

static void __exit spi_drv_exit(void)
{
	/* 反注册spi_driver */
	spi_unregister_driver(&spi_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(spi_drv_init);
module_exit(spi_drv_exit);

MODULE_LICENSE("GPL");


