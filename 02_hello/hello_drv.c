#include <kvm/iodev.h>

#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/raw.h>
#include <linux/tty.h>
#include <linux/capability.h>
#include <linux/ptrace.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/backing-dev.h>
#include <linux/shmem_fs.h>
#include <linux/splice.h>
#include <linux/pfn.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/uio.h>

#include <linux/uaccess.h>


static int major;
static unsigned char hello_buf[100];
static int len;
static int ret;
static struct class *hello_class;
static struct cdev hello_cdev;
static dev_t dev;

static int hello_open (struct inode *node, struct file *filp)
{
	printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
	return 0;
}

static ssize_t hello_read (struct file * filp, char __user *buf, size_t size, loff_t *offset)
{
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	len = size > 100 ? 100 : size;
	copy_to_user(buf, hello_buf, len);
	return len;
}

static ssize_t hello_write (struct file *filp, const char __user *buf, size_t size, loff_t *offset)
{
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	len = size > 100 ? 100 : size;
	copy_from_user(hello_buf, buf, len);
	return len;
}

static int hello_release(struct inode *inode, struct file *file)
{
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	return 0;
}



//create file_operations
static struct file_operations hello_drv = {
	.owner		= THIS_MODULE,
	.open 		= hello_open,
	.read       = hello_read,
	.write		= hello_write,
	.release	= hello_release,
};

//注册结构体
static int hello_init(void)
{
	//major = register_chrdev(0, "hello",&hello_drv);
	if(alloc_chrdev_region(&dev, 0, 3, "hello") < 0)
    {
        printk(KERN_ERR"Unable to alloc_chrdev_region.\n");
        return -EINVAL;
    } 
   
	cdev_init(&hello_cdev, &hello_drv);        
    ret = cdev_add(&hello_cdev, dev, 3);
    if (ret < 0)
    {
        printk(KERN_ERR "Unable to cdev_add.\n");
        return -1;
    }
	//创建类
	hello_class = class_create(THIS_MODULE, "hello_class"); 
	if (IS_ERR(hello_class)) 
	{
		printk(KERN_ERR "class_create() failed for hello_class\n");
		return PTR_ERR(hello_class);

	}

	//创建设备
	device_create(hello_class, NULL, dev, NULL, "hello"); 
	return 0;
}

static void hello_exit(void)
{
	//unregister_chrdev(major, "hello");
	unregister_chrdev_region(dev, 3);
    cdev_del(&hello_cdev);
	device_destroy(hello_class, MKDEV(major, 0));
	class_destroy(hello_class);
}

//入口
module_init(hello_init);

//出口
module_exit(hello_exit);
MODULE_LICENSE("GPL");



