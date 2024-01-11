// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * 	sbertask.c: Test task by SBER
 * 
 *	Copyright (C) 2023 Arsenii Akimov <arseniumfrela@bk.ru>
 *
 *	https://www.github.com/arsaki/test_tasks
 *
 * 	Symbol driver, works as FIFO buffer via char device. 
 * 	Buffers aka queues uses "struct list_head". Queues depth = 1000 bytes. 
 * 	Driver modes:
 *	
 *	*Default  - one buffer,	multiple access
 *	*Single   - one buffer, single access
 * 	*Multi    - multiple buffers, multiple access
 *	
 *	Buffer accept binary data.
 * 	Driver has errors (like overflow) and diagnostic messages
 * 	Linux API compatible
 *
 *	All buffers placed in red black tree.
 *	One buffer consists of list_head elements.
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
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/rbtree.h>

#define BUFFER_DEPTH 1000
#define DEVICE_NAME "sbertask"

#define MODE_DEFAULT 0
#define MODE_SINGLE  1
#define MODE_MULTI   2

/* Each buffer consists of buffer_element's */

struct buffer_element {
	struct list_head list;
	char data;
};

/* Each fifo buffer placed in red black tree. Pid is a key. */

struct rb_buf_node {
	struct 	rb_node node;
	pid_t 	pid;
	struct 	buffer_element *buffer_head;
	struct 	buffer_element *buffer_tail;
	int 	buffer_length;
	int 	write_ready;
	int 	read_ready;
	int 	finished;
	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
};

static DEFINE_SPINLOCK(buffer_lock);
static DEFINE_SPINLOCK(rb_tree_lock);
static struct mutex mode_single_mutex;

static int driver_mode = MODE_DEFAULT;
static char *mode = "default";
module_param(mode, charp, 0000);

static int major_number;
static struct kmem_cache *buffer_cache;

static struct rb_root root = RB_ROOT;

static int add_buffer(pid_t pid)
{
	struct rb_buf_node *new_buffer;
	struct rb_buf_node *buffer;
	struct rb_node **node = &(root.rb_node); 
        struct rb_node *parent = NULL;
	int ret = 0;

	spin_lock(&rb_tree_lock);

	/* Sliding on r.b. tree */
	while (*node) {
	        buffer = container_of(*node, struct rb_buf_node, node);
		parent = *node;
		if (pid < buffer->pid)
			node = &((*node)->rb_left);
	      	else if (pid > buffer->pid)
		      	node = &((*node)->rb_right);
		else if (pid == buffer->pid)
			goto exit;
	}
	new_buffer = kmalloc(sizeof(struct rb_buf_node), GFP_ATOMIC);
	if (new_buffer == NULL){
		pr_err("sbertask: can`t allocate memory for buffer!!!\n");
		ret =  -ENOMEM;
		goto exit;
	}
	/* Add new node and rebalance tree. */
	rb_link_node(&new_buffer->node, parent, node);
	rb_insert_color(&new_buffer->node, &root);
	new_buffer->pid = pid;
	new_buffer->buffer_head = NULL;
	new_buffer->buffer_tail = NULL;
	new_buffer->buffer_length = 0;
	new_buffer->read_ready = 0;
	new_buffer->write_ready = 1;
	new_buffer->finished = 0;
	init_waitqueue_head(&new_buffer->read_wq);
	init_waitqueue_head(&new_buffer->write_wq);

exit:	spin_unlock(&rb_tree_lock);

	return ret;
}


static struct rb_buf_node *get_buffer(pid_t pid)
{
	struct rb_node **node = &(root.rb_node); 
	struct rb_buf_node * buffer;
	
	spin_lock(&rb_tree_lock);
	/* Sliding on tree */
	while (*node) {
	        buffer = container_of(*node, struct rb_buf_node, node);
		if (pid < buffer->pid)
			node = &((*node)->rb_left);
	      	else if (pid > buffer->pid)
		      	node = &((*node)->rb_right);
		else if (pid == buffer->pid){
			spin_unlock(&rb_tree_lock);
			return buffer;
		}
	}
	spin_unlock(&rb_tree_lock);
	pr_err("sbertask: get_buffer(): no buffer found by pid %u\n", current->pid);
	return NULL;
}

static int rm_buffer(pid_t pid)
{
	struct rb_buf_node *rm_buffer;
        struct buffer_element *buffer_entry, *buffer_next;

	spin_lock(&rb_tree_lock);

	rm_buffer = get_buffer(pid);
	if (rm_buffer == NULL){
		pr_err("sbertask: rm_buffer: buffer for pid %u not found\n", pid);
		goto exit;
	}
 	if (rm_buffer->buffer_head != NULL){
                list_for_each_entry_safe(buffer_entry, buffer_next, &rm_buffer->buffer_head->list, list){
                        kmem_cache_free(buffer_cache, buffer_entry);
                }
	}
	rb_erase(&rm_buffer->node, &root);
	kfree(rm_buffer);

exit:	spin_unlock(&rb_tree_lock);

	return 0;
}




static int sbertask_open (struct inode *inode, struct file *file_p)
{
	int ret;
	struct rb_buf_node *buf_node;
	switch (driver_mode){
	case MODE_MULTI:
		/* Just add buffer */
		spin_lock(&rb_tree_lock);
		ret = add_buffer(current->pid);
		buf_node = get_buffer(current->pid);
		buf_node->finished = 0;
		spin_unlock(&rb_tree_lock);
		break;
	case MODE_SINGLE: 
		/* Add buffer and mutex protect */
		if (!mutex_trylock(&mode_single_mutex)){
			return -EBUSY;
		}
		ret = add_buffer(0);
		buf_node = get_buffer(0);
		buf_node->finished = 0;
		break;	
	case MODE_DEFAULT:
		/* Add buffer with pid 0 */
		ret = add_buffer(0);
		buf_node = get_buffer(0);
		buf_node->finished = 0;
		break;
	default:
		pr_err("Undefined behavior in sbertask_open\n");
	}
	if (!ret)
		pr_info("sbertask: process with pid %u opened device\n", current->pid);
	else if (ret == -ENOMEM){ 
		pr_err("sbertask: error - can't allocate buffer memory for pid %u\n", current->pid);
		return -ENOMEM;
	} else 
		pr_err("sbertask: unknown add_buffer() error in sbertask_open(), return code is %d \n", ret);
	
	try_module_get(THIS_MODULE);

	return 0;
};

static int sbertask_release (struct inode *inode, struct file *file_p)
{
	struct rb_buf_node *buf_node;
	switch (driver_mode) {
		case MODE_SINGLE:
			buf_node = get_buffer(0);
			buf_node->finished = 1;
			wake_up_interruptible(&buf_node->read_wq);
			mutex_unlock(&mode_single_mutex);
			break;
		case MODE_DEFAULT:
			buf_node = get_buffer(0);
			buf_node->finished = 1;
			wake_up_interruptible(&buf_node->read_wq);
			break;
		case MODE_MULTI:
			break;
		default:
			pr_err("Undefined_behavior in sbertask_release()\n");
	}
        pr_info("sbertask: process with pid %u closes device\n", current->pid);
	module_put(THIS_MODULE);
	return 0;
};

static  ssize_t sbertask_read (struct file *file_p, char __user *buf, size_t length, loff_t *off_p)
{		
	struct rb_buf_node *buf_node;
	struct buffer_element *queue_iter, *queue_iter_next;
	int c = 0, ret = 0;

	pr_info("sbertask: process with pid %u reads device\n", current->pid);	

	switch (driver_mode) {
		case MODE_DEFAULT:
			spin_lock(&buffer_lock);
			buf_node = get_buffer(0);
			break;
		case MODE_SINGLE:
			buf_node = get_buffer(0);
			break;
		case MODE_MULTI:
			spin_lock(&rb_tree_lock);
                	buf_node = get_buffer(current->pid);
			break;
		default:
			pr_err("Undefined behavior in sbertask_read()\n");
	}
      	if (buf_node == NULL)
		return -EINVAL;	
	/* sleep if empty buffer */
	if(!buf_node->buffer_head || buf_node->buffer_length == 0){
		pr_info("sbertask: queue is empty for process with pid %u\n", current->pid);
		buf_node->read_ready = 0;
		wait_event_interruptible(buf_node->read_wq, buf_node->read_ready || buf_node->finished);
		if ( !buf_node->read_ready || !buf_node->buffer_length) {
			pr_info("sbertask: go to exit\n");
			goto exit;
		}
	}
	/* it is time to send byte and delete entry */
	if (buf_node->buffer_length)
		list_for_each_entry_safe(queue_iter, queue_iter_next, &buf_node->buffer_head->list, list){
			if((c < buf_node->buffer_length) && (c < length)){
				if(put_user(queue_iter->data, buf + c)){
					pr_err("sbertask: can't put data to userspace!\n");
					ret = -EINVAL;
					goto exit;
				}
				pr_info("sbertask: sended '%c'\n", queue_iter->data);
				list_del(&queue_iter->list);
				kmem_cache_free(buffer_cache, queue_iter);
				c++;
			} else
				break;
		}
	ret = c;
	buf_node->buffer_length -= c;
	if (buf_node->buffer_length == 0)
		buf_node->read_ready = 0;
	buf_node->write_ready = 1;
	wake_up_interruptible(&buf_node->write_wq);

exit:	if (driver_mode == MODE_DEFAULT)
		spin_unlock(&buffer_lock);
	if (driver_mode == MODE_MULTI)
		spin_unlock(&rb_tree_lock);
	return ret;
};

static	ssize_t sbertask_write (struct file *file_p, const char __user *buf, size_t length, loff_t *off_p)
{
	struct rb_buf_node * buf_node;
	long unsigned i, ret = 0;

	pr_info("sbertask: process with pid %u writes to device\n", current->pid);	
	switch (driver_mode) {
		case MODE_DEFAULT:
			buf_node=get_buffer(0);
			break;
		case MODE_SINGLE:
			buf_node=get_buffer(0);
			break;
		case MODE_MULTI:
			spin_lock(&rb_tree_lock);
                	buf_node = get_buffer(current->pid);
			break;
		default:
			pr_err("Undefined behavior in sbertask_write()\n");
	}

	if (buf_node == NULL){
		pr_err("sbertask: can't get buffer\n");
		goto exit;
	}
	if (buf_node->buffer_length >= BUFFER_DEPTH){	
		pr_info("sbertask: buffer full\n");
		buf_node->write_ready = 0;
		wait_event_interruptible(buf_node->write_wq, buf_node->write_ready != 0);
	}
	if (buf_node->buffer_length == 0){
		pr_info("sbertask: making new buffer's queue list\n");
		buf_node->buffer_head = kmem_cache_alloc(buffer_cache, GFP_ATOMIC);
		INIT_LIST_HEAD(&buf_node->buffer_head->list);
		buf_node->buffer_tail = buf_node->buffer_head;	
	}
	for (i = 0; i < length && buf_node->buffer_length < BUFFER_DEPTH ; buf++, i++){
		buf_node->buffer_tail = kmem_cache_alloc(buffer_cache, GFP_ATOMIC);
		if (buf_node->buffer_tail == NULL){
			pr_err("sbertask: can't allocate buffer element!\n");
			ret = -EINVAL;
			goto exit;
		}
		list_add_tail(&buf_node->buffer_tail->list, &buf_node->buffer_head->list);
		if (get_user(buf_node->buffer_tail->data, buf)){
			pr_err("sbertask: can't get data from userspace\n");
			ret = -EINVAL;
			goto exit;
		}
		buf_node->buffer_length++;
		pr_info("sbertask: getted '%c'\n", buf_node->buffer_tail->data);
	}
	ret = i;

exit:	if (driver_mode == MODE_DEFAULT)
		spin_unlock(&buffer_lock);
	buf_node->read_ready = 1;
	wake_up_interruptible(&buf_node->read_wq);
	if (driver_mode == MODE_MULTI)
		spin_unlock(&rb_tree_lock);
	return ret;
};



const struct file_operations f_ops = {
	.owner   = THIS_MODULE,
	.open    = sbertask_open,
	.release = sbertask_release,
	.read    = sbertask_read,
	.write   = sbertask_write,
};

static int __init module_start(void)
{
	if 	(!strcmp(mode, "default"))
		driver_mode = MODE_DEFAULT;
	else if (!strcmp(mode, "single"))
		driver_mode = MODE_SINGLE;
	else if (!strcmp(mode, "multi"))
		driver_mode = MODE_MULTI;
	else {
		pr_err("sbertask: wrong mode setted. Only default/single/multi modes supported\n");
		return -EINVAL;
	};

	pr_info("sbertask: module runned in %s mode\n", mode);
	
	/* Register /dev/sbertask */
	major_number = register_chrdev(0, DEVICE_NAME, &f_ops);
	if (major_number < 0){
		pr_err("sbertask: can't register device %s\n", DEVICE_NAME);
		return -ENXIO;
	}
	pr_info("sbertask: assigned major number %d\n", major_number);

	/* Cache create */
	buffer_cache = kmem_cache_create("sbertask_buffer", sizeof(struct buffer_element), 0, SLAB_HWCACHE_ALIGN, NULL);
	if (buffer_cache == NULL){
		pr_err("sbertask: can't create buffer cache\n");
		unregister_chrdev(major_number, DEVICE_NAME);
		return -ENOMEM;
	}

	if (driver_mode == MODE_SINGLE){
		mutex_init(&mode_single_mutex);
	}
		
	pr_info("sbertask: module successfully loaded\n");

	return 0;


};

static void __exit module_stop(void)
{
	struct rb_node *node;
	/* Iterate over rb tree */
	for (node = rb_first(&root); node; node = rb_next(node)){
		struct rb_buf_node *buf_node;
                buf_node = container_of( node, struct rb_buf_node, node);
		switch (driver_mode){
		case MODE_DEFAULT:
		case MODE_SINGLE:
			rm_buffer(0);
			break;
		case MODE_MULTI:	
			rm_buffer(buf_node->pid);	
			break;
		default:
			pr_err("sbertask: indefined behavior in module_stop()\n");
		}
	}
	kmem_cache_destroy(buffer_cache);
	unregister_chrdev(major_number, DEVICE_NAME);
	pr_info("sbertask: module unloaded\n");
};

module_init(module_start);
module_exit(module_stop);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arsenii Akimov <arseniumfrela@bk.ru>");
MODULE_DESCRIPTION("FIFO buffer driver. Runs 3 modes: default, single, multi");
MODULE_PARM_DESC(mode_string, "Select  mode: default/single/multi");

