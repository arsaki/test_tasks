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

static char *buffer;

static int sbertask_open (struct inode *inode, struct file *file_p)
{
	return 0;
};

static  ssize_t sbertask_read (struct file *file_p, char __user *buf, size_t count, loff_t *off_p)
{
	int n = copy_to_user(buf, buffer, BUFFER_SIZE);
	return n;
};

static	ssize_t sbertask_write (struct file *file_p, const char __user *buf, size_t count, loff_t *off_p)
{
	int n = copy_from_user(buffer, buf, (count > BUFFER_SIZE ? BUFFER_SIZE : count));
	return n;
};

static loff_t sbertask_llseek (struct file *file_p, loff_t offset, int whence)
{
	return 0;
};


static int sbertask_release (struct inode *inode, struct file *file_p)
{
	return 0;
};

struct file_operations fops = {
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
	pr_info("sbertask: mode_string = %s\n", mode_string);
	buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
	if (buffer == NULL)
		return 1;
	for (buff_count = 0; buff_count < BUFFER_SIZE; buff_count++)
		buffer[buff_count] = 'a';
	return 0;
}

static void __exit module_stop(void)
{
	pr_info("sbertask module unloaded\n");
}

module_init(module_start);
module_exit(module_stop);



