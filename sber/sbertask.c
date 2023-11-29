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
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/slab_def.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arsenii Akimov <arseniumfrela@bk.ru>");
MODULE_DESCRIPTION("FIFO buffer driver. Runs 3 modes: default, single, multiple");

static char *mode_string = "default";
module_param(mode_string, charp, 0000);
MODULE_PARM_DESC(mode_string, "Select  mode: default/single/multiple");

#define QUEUE_DEPTH 1000
#define DEVICE_NAME "sbertask"

static int major_number;
static struct kmem_cache *queue_cache;
static int queue_length;

struct queue {
	struct list_head list;
	char data;
};

struct queue *queue_head = NULL;
struct queue *queue_tail = NULL;
DEFINE_MUTEX(read_mutex);
DEFINE_SPINLOCK(queue_lock);

static int sbertask_open (struct inode *inode, struct file *file_p)
{
	try_module_get(THIS_MODULE);
	pr_info("sbertask: device %s opened\n", DEVICE_NAME);
	return 0;
};

static  ssize_t sbertask_read (struct file *file_p, char __user *buf, size_t length, loff_t *off_p)
{		
	struct queue *old_entry;
	pr_info("sbertask: read\n");

	if(!queue_length){
		pr_info("sbertask: queue is empty\n");
		if (!mutex_lock_interruptible(&read_mutex)){
			if(mutex_lock_interruptible(&read_mutex)==-EINTR){
				pr_info("Interrupt signal caught 2");
				return 0;
			}
			pr_info("Interrupt signal caught 1");
			return 0;
		}
	}
	if (queue_head == NULL){
		pr_err("sbertask: queue_head is NULL!!!!");
		return 0;
	}
	spin_lock(&queue_lock);
	if(put_user(queue_head->data, buf)){
		spin_unlock(&queue_lock);
		pr_err("sbertask: can't put data to userspace!\n");
		return -EINVAL;
	}
	pr_info("sbertask: sended char %c\n", queue_head->data);

	if(queue_length > 1){
		old_entry = queue_head;
		queue_head = list_entry(queue_head->list.next, struct queue, list);
		list_del(&old_entry->list);
		kmem_cache_free(queue_cache, old_entry);
	}	
	if(queue_length <= 1){
		kmem_cache_free(queue_cache, queue_head);
		queue_head = NULL;
		queue_tail = NULL;
	}
	queue_length--;
	spin_unlock(&queue_lock);
	return 1;
};

static	ssize_t sbertask_write (struct file *file_p, const char __user *buf, size_t length, loff_t *off_p)
{
	pr_info("sbertask: write\n");
	if (queue_length >= QUEUE_DEPTH){	
		pr_info("sbertask: queue full");
		return 0;
	}
	spin_lock(&queue_lock);
	if (queue_length == 0){
		pr_info("sbertask: making new queue list");
		queue_head = kmem_cache_alloc(queue_cache, GFP_KERNEL);
		INIT_LIST_HEAD(&queue_head->list);
		queue_tail = queue_head;	
	} else {
		queue_tail = kmem_cache_alloc(queue_cache, GFP_KERNEL);
		if (queue_tail == NULL)	{
			pr_err("sbertask: can't allocate queue element!\n");
			return -EINVAL;
		}
		list_add_tail(&queue_tail->list, &queue_head->list);
	};
	if (get_user (queue_tail->data, buf)){
		pr_err("sbertask: can't get data from userspace\n");
		return -EINVAL;
	}
	mutex_unlock(&read_mutex);
	queue_length++;
	spin_unlock(&queue_lock);
	pr_info("sbertask: getted '%c'", queue_tail->data);
	return 1;
};


static int sbertask_release (struct inode *inode, struct file *file_p)
{
	module_put(THIS_MODULE);
	mutex_unlock(&read_mutex);
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
	int ret;
	pr_info("sbertask: mode %s\n", mode_string);
	major_number = register_chrdev(0, DEVICE_NAME, &f_ops);
	if (major_number < 0){
		pr_err("Can't register device %s", DEVICE_NAME);
		ret = 2;
		goto register_error;
	}
	pr_info("sbertask: assigned major number %d\n", major_number);
	queue_cache = kmem_cache_create("sbertask_queue", sizeof(struct queue), 0, SLAB_HWCACHE_ALIGN, NULL);
	if (queue_cache == NULL){
		pr_err("sbertask: can't create queue cache\n");
		ret = 3;
		goto cache_error;
	}
	mutex_init(&read_mutex);
	if (mutex_lock_interruptible(&read_mutex)){
		ret = 1;
		goto mutex_error;
	} else{
		pr_info("sbertask: module successfully loaded\n");
		return 0;
	}
mutex_error:
	kmem_cache_destroy(queue_cache);
cache_error:
	unregister_chrdev(major_number, DEVICE_NAME);
register_error:
	return ret;		
	
};

static void __exit module_stop(void)
{
	struct queue *queue_entry, *queue_next;
	int i;
	unregister_chrdev(major_number, DEVICE_NAME);
	if (queue_head != NULL)
		list_for_each_entry_safe(queue_entry, queue_next, &queue_head->list, list){
        		kmem_cache_free(queue_cache, queue_entry);
			pr_info("sbertask: queue counter cache free %i", i);
		}
	kmem_cache_destroy(queue_cache);
	pr_info("sbertask: module unloaded\n");
};

module_init(module_start);
module_exit(module_stop);



