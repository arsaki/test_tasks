// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * 	sbertask.c: Test task by SBER
 * 
 *	Copyright (C) 2023 Arsenii Akimov <arseniumfrela@bk.ru>
 *
 *	https://www.github.com/arsaki/test_tasks
 *
 * 	Symbol driver, works as FIFO buffer via char device. 
 * 	Buffers aka queues uses "struct list_head". Queues depth = 1000 байт.
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
#include <linux/sched.h>
#include <linux/rbtree.h>

#define MODE_DEFAULT 0
#define MODE_SINGLE 1
#define MODE_MULTIPLE 2

static int mode_selector = MODE_DEFAULT;
static char *mode = "default";
module_param(mode, charp, 0000);

#define BUFFER_DEPTH 1000
#define DEVICE_NAME "sbertask"

static int major_number;
static struct kmem_cache *buffer_cache;

/* Each buffer consists of buffer_element's */

struct buffer_element {
	struct list_head list;
	char data;
};

/* Each fifo buffer placed in red black tree. Pid is a key. */

struct rb_buf_node {
	struct rb_node node;
	struct buffer_element *buffer_head;
	struct buffer_element *buffer_tail;
	int buffer_length;
	pid_t pid;
};

static struct rb_root root = RB_ROOT;

DEFINE_SPINLOCK(buffer_lock);
DEFINE_SPINLOCK(rb_tree_lock);

static int add_buffer(pid_t pid)
{
	struct rb_buf_node *new_buffer;
	struct rb_buf_node *buffer;
	struct rb_node **node = &(root.rb_node); 
        struct rb_node *parent = NULL;

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
		spin_unlock(&rb_tree_lock);
		return 1;
	}
	/* Add new node and rebalance tree. */
	rb_link_node(&new_buffer->node, parent, node);
	rb_insert_color(&new_buffer->node, &root);
	new_buffer->pid = pid;
	new_buffer->buffer_head = NULL;
	new_buffer->buffer_tail = NULL;
	new_buffer->buffer_length = 0;
exit:	
	spin_unlock(&rb_tree_lock);

	return 0;
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
	try_module_get(THIS_MODULE);
	ret = add_buffer(current->pid);
	if (!ret)
		pr_info("sbertask: process with pid %u successfully opened device\n", current->pid);
	else{ 
		pr_err("sbertask: error - can't allocate buffer memory for pid %u\n", current->pid);
		return -ENOSPC;
	}

	return 0;
};

static int sbertask_release (struct inode *inode, struct file *file_p)
{
	module_put(THIS_MODULE);
        pr_info("sbertask: process with pid %u closed device\n", current->pid);
	return 0;
};

static  ssize_t sbertask_read (struct file *file_p, char __user *buf, size_t length, loff_t *off_p)
{		
	struct rb_buf_node *tmp_buf_node;
	struct buffer_element *buffer_head, *buffer_tail, *queue_iter, *queue_iter_next;
	int buffer_length, c = 0, ret;
	pr_info("sbertask: process with pid %u read device\n", current->pid);	
	spin_lock(&buffer_lock);
	tmp_buf_node = get_buffer(current->pid);
      	if (tmp_buf_node == NULL)
		return -EINVAL;	
	buffer_head   = tmp_buf_node->buffer_head;	
	buffer_tail   = tmp_buf_node->buffer_tail;	
	buffer_length = tmp_buf_node->buffer_length;
	if(!buffer_head){
		pr_info("sbertask: queue is empty for process with pid %u\n", current->pid);
		ret = 0;
		goto exit;
	}
	if(put_user(buffer_head->data, buf)){
		pr_err("sbertask: can't put data to userspace!\n");
		ret = -EINVAL;
		goto exit;
	}
	/* time to delete entry */
	list_for_each_entry_safe(queue_iter, queue_iter_next, &buffer_head->list, list){
		if((c < buffer_length) && (c < length)){
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
exit:	spun_unlock(&buffer_lock)
	return ret;
};

static	ssize_t sbertask_write (struct file *file_p, const char __user *buf, size_t length, loff_t *off_p)
{
	struct rb_buf_node * buf_node;
	long unsigned i, ret = 0;

	pr_info("sbertask: process with pid %u write device\n", current->pid);	

	spin_lock(&buffer_lock);

	buf_node = get_buffer(current->pid);

	if (buf_node == NULL){
		pr_err("sbertask: can't get buffer\n");
		goto exit;
	}
	if (buf_node->buffer_length >= BUFFER_DEPTH){	
		pr_info("sbertask: buffer full\n");
		goto exit;
	}
	

	if (buf_node->buffer_length == 0){
		pr_info("sbertask: making new buffer's queue list\n");
		buf_node->buffer_head = kmem_cache_alloc(buffer_cache, GFP_ATOMIC);
		INIT_LIST_HEAD(&buf_node->buffer_head->list);
		buf_node->buffer_tail = buf_node->buffer_head;	
	}
	for (i = 0; i < length; buf++, i++){
		buf_node->buffer_tail = kmem_cache_alloc(buffer_cache, GFP_ATOMIC);
		if (buf_node->buffer_tail == NULL)	{
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
		pr_info("sbertask: getted char '%c'\n", buf_node->buffer_tail->data);
	}
	ret = i;

exit:	spin_unlock(&buffer_lock);

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
		mode_selector = MODE_DEFAULT;
	else if (!strcmp(mode, "single"))
		mode_selector = MODE_SINGLE;
	else if (!strcmp(mode, "multiple"))
		mode_selector = MODE_MULTIPLE;
	else {
		pr_err("sbertask: wrong mode setted. Only default/single/multiple modes supported\n");
		return 1;
	};


	pr_info("sbertask: mode is %s\n", mode);
	
	/* Register /dev/sbertask */
	major_number = register_chrdev(0, DEVICE_NAME, &f_ops);
	if (major_number < 0){
		pr_err("sbertask: can't register device %s", DEVICE_NAME);
		return 2;
	}
	pr_info("sbertask: assigned major number %d\n", major_number);

	/* Cache create */
	buffer_cache = kmem_cache_create("sbertask_buffer", sizeof(struct buffer_element), 0, SLAB_HWCACHE_ALIGN, NULL);
	if (buffer_cache == NULL){
		pr_err("sbertask: can't create buffer cache\n");
		unregister_chrdev(major_number, DEVICE_NAME);
		return 3;
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
		rm_buffer(buf_node->pid);	
	}
	kmem_cache_destroy(buffer_cache);
	unregister_chrdev(major_number, DEVICE_NAME);
	pr_info("sbertask: module unloaded\n");
};

module_init(module_start);
module_exit(module_stop);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arsenii Akimov <arseniumfrela@bk.ru>");
MODULE_DESCRIPTION("FIFO buffer driver. Runs 3 modes: default, single, multiple");
MODULE_PARM_DESC(mode_string, "Select  mode: default/single/multiple");

