#include <linux/module.h>
#include <linux/poll.h>
#include <linux/input.h>
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

//作者g生rsefsdf

static int irq;
static int isr_cont = 0;
static u64 irda[68];

struct input_dev *hs0038_input_dev;
static struct timer_list release_timer;
static unsigned int last_key = KEY_RESERVED;
static bool key_pressed = false;

//fsfsfsfsfsfsfosfjgopsgdsiglod'hkipodtihopdfjuohpjdophjodfh
static void release_timer_callback(unsigned long _data)
{
	if(key_pressed)
	{
		input_report_key(hs0038_input_dev, last_key, 0);
		input_sync(hs0038_input_dev);
		key_pressed = false;
		printk("Key released by timer\n");
	}
}

void irda_para_datas(int* value)
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

	*value = (buf_tmp[0] << 8 | buf_tmp[2]);


	return;
	
err:
	isr_cont = 0;
	*value = KEY_RESERVED;
	return;
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
	//struct gpio_desc *gpio_desc = dev_id;
	unsigned int value;
	//printk("gpio_key_isr key %d irq happened\n", gpio_desc->gpio);
	static int num =0;
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
			isr_cont = 0;
			if(key_pressed)
			{
				mod_timer(&release_timer, jiffies + msecs_to_jiffies(150));
				if(num == 0 || num == 3)
				{
					//上报数据
					input_event(hs0038_input_dev, EV_KEY,last_key, 2);
					input_sync(hs0038_input_dev);
					
					num=0;
				}
				num++;;
			}
			return IRQ_HANDLED;
		}		
	}

	
	//一个数据周期为64个中断 2n  (n为字节)	 +3 = 2*32 +3 +1 = 68 
	if(isr_cont == 68)
	{
		irda_para_datas(&value);
		del_timer(&release_timer);
		isr_cont = 0;
		memset(irda, 0x00, sizeof(irda));
		
		if(value != KEY_RESERVED) //解码错误返回
		{
			if(key_pressed && last_key !=value) //上次按下，并且这次键值不等于，则松开
			{
				input_report_key(hs0038_input_dev, last_key, 0);
				input_sync(hs0038_input_dev);
				key_pressed = false;
			}
			//上报数据
			input_event(hs0038_input_dev, EV_KEY, value,1);
			input_sync(hs0038_input_dev);
	
			last_key = value;
			key_pressed = true;
			mod_timer(&release_timer, jiffies + msecs_to_jiffies(150));
		}
		else
		{
			if(key_pressed) //上次按下，则松开
			{
				input_report_key(hs0038_input_dev, last_key, 0);
				input_sync(hs0038_input_dev);
				key_pressed = false;
			}
		}
		
		return IRQ_HANDLED;
	}

	//2.25*32+9.5+4.5 = 85.5
	//mod_timer(&gpio_desc->key_timer, jiffies + msecs_to_jiffies(100));
	
	return IRQ_HANDLED;

}


/* 在入口函数 */
static int gpio_drv_probe(struct platform_device *pdev)
{
    int err = 0;
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);


	 // 从设备树中获取中断号
    irq = platform_get_irq(pdev, 0);
    if (irq < 0) {
        return irq;
    }

	err = request_irq(irq, gpio_key_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "irda_pin", NULL);

	//设置定时器
	setup_timer(&release_timer, release_timer_callback, 0);
	
	//输入系统代码
	//分配input_device
	hs0038_input_dev = devm_input_allocate_device(&pdev->dev);
	if (!hs0038_input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}
	//设置input_device
	hs0038_input_dev->name = "hs0038";
	hs0038_input_dev->id.bustype = BUS_HOST;

	//支持哪类事件
	__set_bit(EV_KEY, hs0038_input_dev->evbit);
	//__set_bit(EV_REP, hs0038_input_dev->evbit); //重复事件

	//支持哪些事件
	/// 设置所有按键支持
    bitmap_set(hs0038_input_dev->keybit, 0, KEY_MAX + 1);
	//注册input_device
	err = input_register_device(hs0038_input_dev);
	
	
	return err;
}

/* 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数
 */
static int gpio_drv_remove(struct platform_device *pdev)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	input_unregister_device(hs0038_input_dev);

	free_irq(irq, NULL);
	del_timer(&release_timer);
	

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


