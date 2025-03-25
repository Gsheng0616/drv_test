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
    {115, 0, "motor_gpio0",},
    {116, 0, "motor_gpio1",},
    {117, 0, "motor_gpio2",},
    {118, 0, "motor_gpio3",},
};

/* 主设备号                                                                 */
static int major = 0;
static struct class *gpio_class;
int motor_pin_ctl[8] = {0x2,0x3,0x1,0x9,0x8,0xc,0x4,0x6};
int motor_pin_index =0;


void set_pin_for_motor(int index)
{
	int i;
	for(i = 0; i< 4; i++)
	{
		gpio_set_value(gpios[i].gpio, motor_pin_ctl[index] & (1 << i) ? 1 : 0);
	}
}

void motor_disable(void)
{
	int i;
	for(i = 0; i< 4; i++)
	{
		gpio_set_value(gpios[i].gpio, 0);
	}
}

/* 
	buf[0] = 步进的步数 		 > 0 逆时针旋转		|  < 0 顺时针旋转                
	buf[1] = mdelay
*/
static ssize_t motor_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    int tmp_buf[2];
	int step;
	int err;
	
	if(size != 8)
		return -EINVAL;
	
    err = copy_from_user(tmp_buf, buf, size);

	//逆时针旋转
	if(tmp_buf[0] > 0)
	{
		for(step = 0; step < tmp_buf[0]; step++)
		{
			set_pin_for_motor(motor_pin_index);
			mdelay(tmp_buf[1]);
			motor_pin_index --;
			if(motor_pin_index == -1)
				motor_pin_index = 7;
		}
	}
	else
	{
		tmp_buf[0] = 0 - tmp_buf[0];
		for(step = 0; step < tmp_buf[0]; step++)
		{
			set_pin_for_motor(motor_pin_index);
			mdelay(tmp_buf[1]);
			motor_pin_index ++;
			if(motor_pin_index == 8)
				motor_pin_index = 0;
		}
	}

	motor_disable();
    return size;    
}


/* 定义自己的file_operations结构体                                              */
static struct file_operations motor_drv = {
	.owner	 = THIS_MODULE,
	.write   = motor_write,
};


/* 在入口函数 */
static int __init gpio_drv_init(void)
{
    int err;
    int i;
    int count = sizeof(gpios)/sizeof(gpios[0]);
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	
	for (i = 0; i < count; i++)
	{		
		err = gpio_request(gpios[i].gpio, gpios[i].name);

		gpio_direction_output(gpios[i].gpio, 0);
	}

	/* 注册file_operations 	*/
	major = register_chrdev(0, "motor_28byz", &motor_drv);  /* /dev/gpio_desc */

	gpio_class = class_create(THIS_MODULE, "100ask_motor_class");
	if (IS_ERR(gpio_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "motor_28byz");
		return PTR_ERR(gpio_class);
	}

	device_create(gpio_class, NULL, MKDEV(major, 0), NULL, "100ask_motor"); /* /dev/100ask_gpio */
	
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
	unregister_chrdev(major, "motor_28byz");

	for (i = 0; i < count; i++)
	{
		gpio_free(gpios[i].gpio);
	}
}


/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(gpio_drv_init);
module_exit(gpio_drv_exit);

MODULE_LICENSE("GPL");


