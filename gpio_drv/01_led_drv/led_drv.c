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

static struct gpio_desc gpios[2] = {
    {131, 0, "gpio_100ask_1", },
    //{132, 0, "gpio_100ask_2", },
};

/* 主设备号                                                                 */
static int major = 0;
static struct class *led_class;




/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t gpio_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	char tmp_buf[2];

	int count = sizeof(gpios)/sizeof(gpios[0]);
	if(size != 2)
	{
		return -EINVAL;
	}

	copy_from_user(tmp_buf, buf, 1);

	if(tmp_buf[0] > count)
	{
		return -EINVAL;
	}

	tmp_buf[1] = gpio_get_value(gpios[tmp_buf[0]].gpio);
	
	copy_to_user(buf, tmp_buf, size);
	
	return size;
}

static ssize_t gpio_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
 	char tmp_buf[2];

	int count = sizeof(gpios)/sizeof(gpios[0]);
	if(size != 2)
	{
		return -EINVAL;
	}

	copy_from_user(tmp_buf, buf, size);

	if(tmp_buf[0] > count)
	{
		return -EINVAL;
	}

	gpio_set_value(gpios[tmp_buf[0]].gpio, tmp_buf[1]);
	
	
	return size;
}


/* 定义自己的file_operations结构体                                              */
static struct file_operations gpio_led_drv = {
	.read    = gpio_drv_read,
	.write   = gpio_drv_write,
};



/* 在入口函数 */
static int __init led_drv_init(void)
{
    int err;
    int i;
    int count = sizeof(gpios)/sizeof(gpios[0]);
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	
	for (i = 0; i < count; i++)
	{		
		err = gpio_request(gpios[i].gpio, gpios[i].name);
		if(err < 0)
		{
			printk("gpio_request fail! gpio =  %d, gpio_name = %s ",gpios[i].gpio,gpios[i].name);
			return -ENODEV;
		}
		gpio_direction_output(gpios[i].gpio, 1);
	}

	/* 注册file_operations 	*/
	major = register_chrdev(0, "100ask_led", &gpio_led_drv);  /* /dev/gpio_desc */

	led_class = class_create(THIS_MODULE, "100ask_led_class");
	if (IS_ERR(led_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_led");
		return PTR_ERR(led_class);
	}

	device_create(led_class, NULL, MKDEV(major, 0), NULL, "100ask_led"); /* /dev/100ask_gpio */
	
	return err;
}

/* 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数
 */
static void __exit led_drv_exit(void)
{
    int i;
    int count = sizeof(gpios)/sizeof(gpios[0]);
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	device_destroy(led_class, MKDEV(major, 0));
	class_destroy(led_class);
	unregister_chrdev(major, "100ask_led");

	for (i = 0; i < count; i++)
	{
		gpio_free(gpios[i].gpio);
	}
}


/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(led_drv_init);
module_exit(led_drv_exit);

MODULE_LICENSE("GPL");


