/*
 * Simple synchronous userspace interface to SPI devices
 *
 * Copyright (C) 2006 SWAPP
 *	Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/acpi.h>

#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>


#define SPI_IOC_WR 123

/*-------------------------------------------------------------------------*/

static struct spi_device *oled_device;
static int major;

static struct gpio_desc* gpio_dc;
#define OLED_CMD  0
#define OLED_DATA 1
#define OLED_IOC_INIT    123
#define OLED_IOC_SET_POS 124


static struct fb_info *myfb_info;
static unsigned int pseudo_palette[16];
struct task_struct *kthread;

unsigned char* oled_buf;

extern void oled_set_pos(int col,int page);

static void oled_dc_init(void)
{
	//设置dc引脚为输出 默认为高
	gpiod_direction_output(gpio_dc, 1);
}

static void oled_set_dc_pin(int value)
{
	gpiod_set_value(gpio_dc, value);
}

void oled_write_cmd_data(unsigned char dc_data, unsigned char dc_cmd)
{
	if(dc_cmd == 1) //数据
	{
		oled_set_dc_pin(1);
	}
	else  //命令
	{
		oled_set_dc_pin(0);
	}

	spi_write(oled_device, &dc_data, 1);
}

static int oled_kthread_func(void *__partid)
{
	int i,col,line,bit,k,page;
	unsigned char *p[8];
	unsigned char data[8];
	unsigned char byte;
	unsigned char* fb = myfb_info->screen_base;
	
	while (!kthread_should_stop()) 
	{
		k=0;
		for(i=0;i<8;i++) //64/8 = 8页
		{
			for(line=0;line<8;line++) //每次处理8行,获得fb中每一行的首地址
			{
				p[line]= &fb[i*128 + line*16];
			}

			for(col=0;col<16;col++) //处理128列的128bit，一行128/8=16个字节
			{
				for(line=0;line<8;line++) //取出第k行的8bit数据
				{
					data[line] = *p[line]++;
				}

				//处理数据,重新组合
				for(bit=7;bit>=0;bit--)
				{
					
					byte = (((data[0]>>bit) & 0x01) << 0)|
							(((data[1]>>bit) & 0x01)<< 1)|
							(((data[2]>>bit) & 0x01)<< 2)|
							(((data[3]>>bit) & 0x01)<< 3)|
							(((data[4]>>bit) & 0x01)<< 4)|
							(((data[5]>>bit) & 0x01)<< 5)|
							(((data[6]>>bit) & 0x01)<< 6)|
							(((data[7]>>bit) & 0x01)<< 7);
					oled_buf[k++] = byte;
				}
			}
		}

		//通过spi发送数据给oled
		for(page = 0;page<8; page++)
		{
			oled_set_pos(0, page);
			oled_set_dc_pin(1);
			spi_write(oled_device, &oled_buf[page*128], 128);
		}	

		/* 4. 休眠一会 */
		schedule_timeout_interruptible(HZ);
	}
	return 0;
}

static int oled_init_func(void)
{
	oled_write_cmd_data(0xae, OLED_CMD);
	
	oled_write_cmd_data(0xd5, OLED_CMD);
	oled_write_cmd_data(0x80, OLED_CMD);
	
	oled_write_cmd_data(0xa8, OLED_CMD);
	oled_write_cmd_data(0x3f, OLED_CMD);
	
	oled_write_cmd_data(0xd3, OLED_CMD);
	oled_write_cmd_data(0x00, OLED_CMD);
	
	oled_write_cmd_data(0x40, OLED_CMD);
	
	oled_write_cmd_data(0x8d, OLED_CMD);
	oled_write_cmd_data(0x10, OLED_CMD);
	
	oled_write_cmd_data(0xa1, OLED_CMD);
	
	oled_write_cmd_data(0xc8, OLED_CMD);
	
	oled_write_cmd_data(0xda, OLED_CMD);
	oled_write_cmd_data(0x12, OLED_CMD);
	
	oled_write_cmd_data(0x81, OLED_CMD);
	oled_write_cmd_data(0x66, OLED_CMD);
	
	oled_write_cmd_data(0xd9, OLED_CMD);
	oled_write_cmd_data(0x22, OLED_CMD);

	oled_write_cmd_data(0xdb, OLED_CMD);
	oled_write_cmd_data(0x30, OLED_CMD);
	
	oled_write_cmd_data(0xa4, OLED_CMD);
	
	oled_write_cmd_data(0xa6, OLED_CMD);
	oled_write_cmd_data(0xaf,OLED_CMD);//set dispkay on

	
	return 0;
}

void oled_set_pos(int col,int page)
{
	oled_write_cmd_data(0xb0 | page, OLED_CMD);
	oled_write_cmd_data(col&0x0f, OLED_CMD);
	oled_write_cmd_data(((col&0xf0)>>4)|0x10, OLED_CMD);
}
static long oled_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int col,page;

	switch (cmd)
	{
		case OLED_IOC_INIT:
			{
				oled_dc_init(); //初始化dc引脚
				oled_init_func();	//初始化oled
				break;
			}
		case OLED_IOC_SET_POS: //设置起始位置
			{
				col  = arg & 0xff;
				page = (arg >> 8) & 0xff;
				
				oled_set_pos(col, page);
				break;
			}
	}
	

	
	return 0;
}
static ssize_t oled_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	char * kernel_buff;
	int err;
	
	kernel_buff = kmalloc(count, GFP_KERNEL);
	err = copy_from_user(kernel_buff, buf, count);

	oled_set_dc_pin(1); //拉高dc引脚，表示写数据
	spi_write(oled_device, kernel_buff, count);
	
	kfree(kernel_buff);
	
	return count;
}

static const struct file_operations oled_fops = {
	.owner =	THIS_MODULE,
	/* REVISIT switch to aio primitives, so that userspace
	 * gets more complete API coverage.  It'll simplify things
	 * too, except for the locking.
	 */
	.write = oled_write,
	.unlocked_ioctl = oled_ioctl,
};

/*-------------------------------------------------------------------------*/

/* The main reason to have this class is to make mdev/udev create the
 * /dev/oledB.C character device nodes exposing our userspace API.
 * It also simplifies memory management.
 */

static struct class *oled_class;

static const struct of_device_id oled_dt_ids[] = {
	{ .compatible = "vhd,oled" },
	{},
};
	
/* from pxafb.c */
static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int mylcd_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;

	/* dprintk("setcol: regno=%d, rgb=%d,%d,%d\n",
		   regno, red, green, blue); */

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */

		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);

			pal[regno] = val;
		}
		break;

	default:
		return 1;	/* unknown type */
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
static struct fb_ops myfb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= mylcd_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int oled_probe(struct spi_device *spi)
{
	dma_addr_t phy_addr;
	/* 1. 记录spi_device */
	oled_device = spi;
	

	//获取gpio
	gpio_dc =  gpiod_get(&spi->dev, "dc", 0);
	
	/* 2. 注册字符设备 */
	major = register_chrdev(0, "vhd_oled", &oled_fops);
	oled_class = class_create(THIS_MODULE, "vhd_class_oled");
	device_create(oled_class, NULL, MKDEV(major, 0), NULL, "vhd_oled");	

	/* A. 分配fb_info */
	myfb_info = framebuffer_alloc(0, NULL);

	/* B. 设置fb_info */
	/* B.1 var : LCD分辨率、颜色格式 */
	myfb_info->var.xres_virtual = myfb_info->var.xres = 128;
	myfb_info->var.yres_virtual = myfb_info->var.yres = 64;
	
	myfb_info->var.bits_per_pixel = 1; 	

	/* B.2 fix */
	strcpy(myfb_info->fix.id, "100ask_oled");
	myfb_info->fix.smem_len = myfb_info->var.xres * myfb_info->var.yres * myfb_info->var.bits_per_pixel / 8;

	myfb_info->flags |= FBINFO_MODULE; /* 禁止显示LOGO */


	/* fb的虚拟地址 */
	myfb_info->screen_base = dma_alloc_wc(NULL, myfb_info->fix.smem_len, &phy_addr,
					 GFP_KERNEL);
	myfb_info->fix.smem_start = phy_addr;  /* fb的物理地址 */
	
	myfb_info->fix.type = FB_TYPE_PACKED_PIXELS;
	myfb_info->fix.visual = FB_VISUAL_MONO10;

	myfb_info->fix.line_length = myfb_info->var.xres * myfb_info->var.bits_per_pixel / 8;	

	/* c. fbops */
	myfb_info->fbops = &myfb_ops;
	myfb_info->pseudo_palette = pseudo_palette;


	/* C. 注册fb_info */
	register_framebuffer(myfb_info);

	//创建oled显存
	oled_buf = kmalloc(1024, GFP_KERNEL);
	oled_dc_init(); //初始化dc引脚
	oled_init_func();	//初始化oled


	//创建线程
	kthread = kthread_run(oled_kthread_func, NULL, "oled_kthread");

	return 0;
}

static int oled_remove(struct spi_device *spi)
{
	kthread_stop(kthread);
	kfree(oled_buf);
	
	/* A. 反注册fb_info */
	unregister_framebuffer(myfb_info);

	/* B. 释放内存 */
	dma_free_wc(NULL, myfb_info->fix.smem_len, myfb_info->screen_base,
		    myfb_info->fix.smem_start);

	/* C. 释放fb_info */
	framebuffer_release(myfb_info);
	
	gpiod_put(gpio_dc);
	/* 反注册字符设备 */
	device_destroy(oled_class, MKDEV(major, 0));
	class_destroy(oled_class);
	unregister_chrdev(major, "vhd_oled");

	return 0;
}

static struct spi_driver oled_spi_driver = {
	.driver = {
		.name =		"vhd_spi_oled_drv",
		.of_match_table = of_match_ptr(oled_dt_ids),
	},
	.probe =	oled_probe,
	.remove =	oled_remove,

	/* NOTE:  suspend/resume methods are not necessary here.
	 * We don't do anything except pass the requests to/from
	 * the underlying controller.  The refrigerator handles
	 * most issues; the controller driver handles the rest.
	 */
};

/*-------------------------------------------------------------------------*/

static int __init oled_init(void)
{
	int status;

	status = spi_register_driver(&oled_spi_driver);
	if (status < 0) {
	}
	return status;
}
module_init(oled_init);

static void __exit oled_exit(void)
{
	spi_unregister_driver(&oled_spi_driver);
}
module_exit(oled_exit);

MODULE_LICENSE("GPL");

