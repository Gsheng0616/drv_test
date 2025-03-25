#include <linux/module.h>
#include <linux/poll.h>
#include <linux/i2c.h> 
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


//static struct gpio_desc *i2c_gpios;
static struct i2c_client *g_client;


/* 主设备号                                                                 */
static int major = 0;
static struct class *i2c_class;


struct fasync_struct *button_fasync;

static DECLARE_WAIT_QUEUE_HEAD(gpio_wait);




/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t i2c_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	int err;
	struct i2c_msg msgs[2];

	/*g_client->adapter 通过i2c控制器传输数据*/
	err = i2c_transfer(g_client->adapter, msgs, 2);

	
	/*拷贝回用户空间*/
	err = copy_to_user(buf, msgs, 2);
	
	return size;
}

static ssize_t i2c_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
	int err;
	struct i2c_msg msgs[2];

	/*从用户空间拷贝数据*/
	err = copy_from_user(msgs, buf, size);
	
	/*g_client->adapter 通过i2c控制器传输数据*/
    err = i2c_transfer(g_client->adapter, msgs, 2);

    
    return size;    
}


static unsigned int i2c_drv_poll(struct file *fp, poll_table * wait)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	poll_wait(fp, &gpio_wait, wait);
	//return is_key_buf_empty() ? 0 : POLLIN | POLLRDNORM;
	return 0;
}

static int i2c_drv_fasync(int fd, struct file *file, int on)
{
	if (fasync_helper(fd, file, on, &button_fasync) >= 0)
		return 0;
	else
		return -EIO;
}


/* 定义自己的file_operations结构体                                              */
static struct file_operations i2c_drv = {
	.owner	 = THIS_MODULE,
	.read    = i2c_drv_read,
	.write   = i2c_drv_write,
	.poll    = i2c_drv_poll,
	.fasync  = i2c_drv_fasync,
};


/* 在入口函数 */
static int i2c_drv_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	//struct i2c_adapter *adapter = client->adapter;  //获取控制器信息
	//struct device_node *np = client->dev.of_node;     //获取节点信息
	g_client = client;

	//获取节点信息

	//注册字符驱动程序
	/* 注册file_operations 	*/
	major = register_chrdev(0, "100ask_i2c", &i2c_drv);  /* /dev/gpio_desc */

	i2c_class = class_create(THIS_MODULE, "100ask_i2c_class");
	if (IS_ERR(i2c_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_i2c");
		return PTR_ERR(i2c_class);
	}

	return 0;
}



/* 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数 */
static int i2c_drv_remove(struct i2c_client *client)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	device_destroy(i2c_class, MKDEV(major, 0));
	class_destroy(i2c_class);
	unregister_chrdev(major, "100ask_gpio_key");


	return 0;
}

static const struct of_device_id i2c_dt_ids[] = {
        { .compatible = "100ask,i2c_device", },
        { /* sentinel */ }
};

static struct i2c_driver i2c_driver = {
	.driver = {
		.name = "i2c_drv",
		.of_match_table = i2c_dt_ids,
	},
	.probe  = i2c_drv_probe,
	.remove = i2c_drv_remove,
};


static int __init i2c_drv_init(void)
{
	/* 注册i2c_driver */
	return i2c_add_driver(&i2c_driver);
}

static void __exit i2c_drv_exit(void)
{
	/* 反注册i2c_driver */
	i2c_del_driver(&i2c_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(i2c_drv_init);
module_exit(i2c_drv_exit);

MODULE_LICENSE("GPL");


