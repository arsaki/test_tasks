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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arsenii Akimov <arseniumfrela@bk.ru>");
MODULE_DESCRIPTION("FIFO buffer driver. Runs 3 modes: default, single, multiple");

static char *mode_string = "default";
module_param(mode_string, charp, 0000);
MODULE_PARM_DESC(mode_string, "Select  mode: default/single/multiple");

#define BUFFER_SIZE 1000
#define DEVICE_NAME "sbertask"

static char *buffer;
static int major_number;
static int reads_count = 1;

static int sbertask_open (struct inode *inode, struct file *file_p)
{
	reads_count = 1;
	try_module_get(THIS_MODULE);
	pr_info("sbertask: device %s opened\n", DEVICE_NAME);
	return 0;
};

static  ssize_t sbertask_read (struct file *file_p, char __user *buf, size_t length, loff_t *off_p)
{	
	if (!reads_count){
		pr_info("sbertask: readed %d bytes\n", 0);
		return 0;
	}
	else{
		int n = copy_to_user(buf,(void *)buffer, length < BUFFER_SIZE ? length : BUFFER_SIZE);
		pr_info("sbertask: readed %d bytes, length is %d\n",(int) (length < BUFFER_SIZE ? length : BUFFER_SIZE) , (int)length);
		reads_count = 0;
		return BUFFER_SIZE;
	}
	return 0;
};

static	ssize_t sbertask_write (struct file *file_p, const char __user *buf, size_t length, loff_t *off_p)
{
	int n = copy_from_user(buffer, buf, (length > BUFFER_SIZE ? BUFFER_SIZE : length));
	pr_info("sbertask: writed %d bytes\n", n);
	return n;
};

static loff_t sbertask_llseek (struct file *file_p, loff_t offset, int whence)
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
	.llseek = sbertask_llseek,
	.release = sbertask_release,
};





static int __init module_start(void)
{
	int buff_count;
	pr_info("sbertask: module successfully loaded\n");
	pr_info("sbertask: mode %s\n", mode_string);
	buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
	if (buffer == NULL){
		pr_err(" Can't allocate memory for buffer\n");
		return 1;
	}
	for (buff_count = 0; buff_count < BUFFER_SIZE; buff_count++)
		buffer[buff_count] = 'a';
	major_number = register_chrdev(0, DEVICE_NAME, &f_ops);
	if (major_number < 0){
		pr_err("Can't register device %s", DEVICE_NAME);
		kfree(buffer);
		return 1;
	}
	else
		pr_info("sbertask: assigned major number %d\n", major_number);
	return 0;
}

static void __exit module_stop(void)
{
	unregister_chrdev(major_number, DEVICE_NAME);
	pr_info("sbertask: module unloaded\n");
}

module_init(module_start);
module_exit(module_stop);



