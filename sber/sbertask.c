// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * 	sbertask.c: Test task by SBER
 * 
 *	Copyright (C) 2023 Arsenii Akimov <arseniumfrela@bk.ru>
 *
 *	https://www.github.com/arsaki/test_tasks
 *
 * 	Symbol driver, works as FIFO buffer via char device. 
 * 	Queue uses "struct list_head". Queue depth = 1000 байт.
 * 	
 * 	Driver modes:
 *	
 *	*Default  - one buffer,	multiple access
 *	*Single   - one buffer, single access
 * 	*Multiple - multiple buffers, multiple access
 *	
 *	Buffer accept binary data.
 * 	Driver has error(like overflow) and diagnostic messages
 * 	Linux API compatible
 *
 */

#include <linux/init.h> 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arsenii Akimov <arseniumfrela@bk.ru>");
MODULE_DESCRIPTION("FIFO buffer driver. Runs 3 modes: default, single, multiple");

static char *mode_string = "default";
module_param(mode_string, charp, 0000);
MODULE_PARM_DESC(mode_string, "Select  mode: default/single/multiple");

#define QUEUE_DEPTH 1000
#define DEVICE_NAME "sbertask"

static int major_number;
static kmem_cache_t *queue_cache;


struct queue {
	struct list_head list;
	char data;
}

static int sbertask_open (struct inode *inode, struct file *file_p)
{
	try_module_get(THIS_MODULE);
	pr_info("sbertask: device %s opened\n", DEVICE_NAME);
	return 0;
};

static  ssize_t sbertask_read (struct file *file_p, char __user *buf, size_t length, loff_t *off_p)
{	
	return 0;
};

static	ssize_t sbertask_write (struct file *file_p, const char __user *buf, size_t length, loff_t *off_p)
{
	return 0;
};


static int sbertask_release (struct inode *inode, struct file *file_p)
{
	module_put(THIS_MODULE);
	pr_info("sbertask: device %s closed\n", DEVICE_NAME);
	return 0;
};

const struct file_operations f_ops = {
	.owner = THIS_MODULE,
	.open  = sbertask_open,
	.read  = sbertask_read,
	.write = sbertask_write,
	.release = sbertask_release,
};

static int __init module_start(void)
{
	pr_info("sbertask: mode %s\n", mode_string);
	major_number = register_chrdev(0, DEVICE_NAME, &f_ops);
	if (major_number < 0){
		pr_err("Can't register device %s", DEVICE_NAME);
		return 1;
	}
	else
		pr_info("sbertask: assigned major number %d\n", major_number);
	queue_cache = kmem_cache_create("sbertask_queue", QUEUE_DEPTH, 0, 0, )



	pr_info("sbertask: module successfully loaded\n");
	return 0;
};

static void __exit module_stop(void)
{
	unregister_chrdev(major_number, DEVICE_NAME);
	pr_info("sbertask: module unloaded\n");
};

module_init(module_start);
module_exit(module_stop);



