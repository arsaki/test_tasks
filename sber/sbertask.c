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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arsenii Akimov <arseniumfrela@bk.ru>");
MODULE_DESCRIPTION("FIFO buffer driver. Runs 3 modes: default, single, multiple");
MODULE_PARM_DESC(mode_string, "Select  mode: default/single/multiple");

static char *mode_string = "default";
module_param(mode_string, charp, 0000);

#define QUEUE_DEPTH 1000
#define DEVICE_NAME "sbertask"

static int major_number;
static struct kmem_cache *queue_cache;

/* Each queue consists of queue_element */

struct queue_element {
	struct list_head list;
	char data;
};

/* Each fifo buffer placed in red black tree. Pid is a key. */

struct rb_buf_node {
	struct rb_node node;
	struct queue_element *queue_head;
	struct queue_element *queue_tail;
	int queue_length;
	pid_t pid;
};

static struct rb_root root = RB_ROOT;

DEFINE_SPINLOCK(queue_lock);
DEFINE_SPINLOCK(rb_tree_lock);

static int add_buffer(pid_t pid)
{
	struct rb_buf_node *new_buffer;
	struct rb_buf_node *buffer_select;
	struct rb_node **node = &(root.rb_node); 
        struct rb_node *parent = NULL;
	int delete_buffer = 1;
	pr_info("add_buffer started\n");
	new_buffer = kmalloc(sizeof(struct rb_buf_node), GFP_KERNEL);
	if (new_buffer == NULL){
		pr_err("sbertask: can`t allocate memory for buffer!!!\n");
		return 1;
	}
	spin_lock(&rb_tree_lock);

	/* If empty tree */
	if (*node == NULL) {
		pr_info("sbertask: creating first rb tree node...\n");
		new_buffer->pid = pid;
		new_buffer->queue_head = NULL;
		new_buffer->queue_tail = NULL;
		new_buffer->queue_length = 0;
		root.rb_node = &(new_buffer->node);
		root.rb_node->rb_right = NULL;
		root.rb_node->rb_left = NULL;
		delete_buffer = 0;
	}
	/* Sliding on tree */
	while (*node) {
		pr_info("begin slide on tree\n");
	        buffer_select = container_of(*node, struct rb_buf_node, node);
		parent = *node;
		if (pid < buffer_select->pid)
			node = &((*node)->rb_left);
	      	else if (pid > buffer_select->pid)
		      	node = &((*node)->rb_right);
		else if (pid == buffer_select->pid){
			pr_info("sbertask: rb tree node founded and already exist\n");
			if (delete_buffer)
				kfree(new_buffer);
			goto exit;
		}
		
	}
	/* Add new node and rebalance tree. */
	pr_info("rb_link_node\n");
	rb_link_node(&new_buffer->node, parent, node);
	pr_info("rb_insert_color\n");
	rb_insert_color(*node, &root);
	pr_info("filling new_buffer fields\n");
	new_buffer->pid = pid;
	new_buffer->queue_head = NULL;
	new_buffer->queue_tail = NULL;
	new_buffer->queue_length = 0;
exit:	
	pr_info("spin_unlock\n");
	spin_unlock(&rb_tree_lock);

	return 0;
}

static struct rb_buf_node *get_buffer(pid_t pid)
{
	struct rb_node **node = &(root.rb_node); 
	struct rb_buf_node * buffer_select;

	spin_lock(&rb_tree_lock);

	/* Sliding on tree */
	while (*node) {
		pr_info("sbertask: get_buffer() searching buffer...\n");
	        buffer_select = container_of(*node, struct rb_buf_node, node);
		if (pid < buffer_select->pid)
			node = &((*node)->rb_left);
	      	else if (pid > buffer_select->pid)
		      	node = &((*node)->rb_right);
		else if (pid == buffer_select->pid){
			pr_info("sbertask: get_buffer() buffer founded!!!\n");

			spin_unlock(&rb_tree_lock);

			return buffer_select;
		}
		
	}

	spin_unlock(&rb_tree_lock);

	pr_err("sbertask: get_buffer() no buffer found by pid %u\n", current->pid);
	return NULL;
}

static int rm_buffer(pid_t pid)
{
	struct rb_buf_node *rm_buffer;
        struct queue_element *queue_entry, *queue_next;
      	int i;


	pr_info("sbertask: rm_buffer executed\n");
	rm_buffer = get_buffer(pid);
	spin_lock(&rb_tree_lock);
 	if (rm_buffer->queue_head != NULL){
		pr_info("rm_buffer->queue_head != NULL\n");
                list_for_each_entry_safe(queue_entry, queue_next, &rm_buffer->queue_head->list, list){
                        kmem_cache_free(queue_cache, queue_entry);
                        pr_info("sbertask: pid %u, queue counter cache free %i\n", current->pid, i);
                }
	}
	pr_info("rm_buffer() rb_erase\n");
	rb_erase(&rm_buffer->node, &root);
	pr_info("rm_buffer() kfree\n");
	kfree(rm_buffer);
	spin_unlock(&rb_tree_lock);
	return 0;
}




static int sbertask_open (struct inode *inode, struct file *file_p)
{
	try_module_get(THIS_MODULE);
	pr_info("sbertask: device %s opened\n", DEVICE_NAME);
	/* Buffer create */
	if (!add_buffer(current->pid))
		pr_info("sbertask: process with pid %u successfully opened device\n", current->pid);
	return 0;
};

static int sbertask_release (struct inode *inode, struct file *file_p)
{
	module_put(THIS_MODULE);
	pr_info("sbertask: release\n");
	/* Buffer delete */
//        if (!rm_buffer(current->pid))
                pr_info("sbertask: process with pid %u closed device\n", current->pid);
	pr_info("sbertask: device %s closed\n", DEVICE_NAME);
	return 0;
};

static  ssize_t sbertask_read (struct file *file_p, char __user *buf, size_t length, loff_t *off_p)
{		
	struct rb_buf_node *tmp_buf_node;
	struct queue_element *queue_head, *queue_tail, *queue_tmp;
	int queue_length, c = 0;
	struct list_head *iterator;
	pr_info("sbertask: process with pid %u read device\n", current->pid);	
	tmp_buf_node = get_buffer(current->pid);
      	if (tmp_buf_node == NULL)
		return -EINVAL;	
	queue_head   = tmp_buf_node->queue_head;	
	queue_tail   = tmp_buf_node->queue_tail;	
	queue_length = tmp_buf_node->queue_length;

	if(!queue_head){
		pr_info("sbertask: queue is empty for process with pid %u\n", current->pid);
		return 0;
	}

	spin_lock(&queue_lock);
	
	if(put_user(queue_head->data, buf)){
		spin_unlock(&queue_lock);
		pr_err("sbertask: can't put data to userspace!\n");
		return -EINVAL;
	}
	/* time to delete entry */

	list_for_each(iterator, &queue_head->list){
		if((c < queue_length) && (c < length)){
			queue_tmp = list_entry(iterator, struct queue_element, list);
			pr_info("begin of read iteration %d \n", c);
			if(put_user(queue_tmp->data, buf )){
				spin_unlock(&queue_lock);
				pr_err("sbertask: can't put data to userspace!\n");
				return -EINVAL;
        		}
			pr_info("sbertask: sended '%c'\n", queue_tmp->data);
//	                list_del(&queue_tmp->list);
//	                kmem_cache_free(queue_cache, queue_tmp);
			pr_info("end of read iteration %d \n", c);
			c++;
		} else
			break;
	}
	pr_info("sbertask: exit sbertask_read\n");
	tmp_buf_node->queue_head = queue_head;	
	tmp_buf_node->queue_tail = queue_tail;	
	tmp_buf_node->queue_length = --queue_length;
	spin_unlock(&queue_lock);
	return c;
};

static	ssize_t sbertask_write (struct file *file_p, const char __user *buf, size_t length, loff_t *off_p)
{
	struct rb_buf_node * tmp_buf_node;
	struct queue_element *queue_head, *queue_tail;
	long unsigned queue_length, c;
	pr_info("sbertask: process with pid %u write device\n", current->pid);	
	pr_info("length is %lu\n",length);
	tmp_buf_node = get_buffer(current->pid);
	if (tmp_buf_node == NULL)
	queue_head = tmp_buf_node->queue_head;
	queue_tail = tmp_buf_node->queue_tail;
	queue_length = tmp_buf_node->queue_length;

	if (queue_length >= QUEUE_DEPTH){	
		pr_info("sbertask: queue full\n");
		return 0;
	}
	
	spin_lock(&queue_lock);

	if (queue_length == 0){
		pr_info("sbertask: making new queue list\n");
		queue_head = kmem_cache_alloc(queue_cache, GFP_KERNEL);
		INIT_LIST_HEAD(&queue_head->list);
		queue_tail = queue_head;	
	}
	for (c = 0; c < length; c++, buf++){
		queue_tail = kmem_cache_alloc(queue_cache, GFP_KERNEL);
		if (queue_tail == NULL)	{
			pr_err("sbertask: can't allocate queue element!\n");
			return -EINVAL;
		}
		list_add_tail(&queue_tail->list, &queue_head->list);
		if (get_user(queue_tail->data, buf)){
			pr_err("sbertask: can't get data from userspace\n");
			return -EINVAL;
		}
		queue_length++;
		pr_info("sbertask: getted '%c', address queue_tail %p , queue_head %p\n", queue_tail->data, queue_tail, queue_head);
	}
	pr_info("c is %lu\n", c);
	
	spin_unlock(&queue_lock);

	tmp_buf_node->queue_head = queue_head;
        tmp_buf_node->queue_tail = queue_tail;
        tmp_buf_node->queue_length = queue_length;

	

	return c;
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
	pr_info("sbertask: ________________________mode %s______________________________\n", mode_string);
	
	/* Register /dev/sbertask */
	major_number = register_chrdev(0, DEVICE_NAME, &f_ops);
	if (major_number < 0){
		pr_err("sbertask: can't register device %s", DEVICE_NAME);
		return 1;
	}
	pr_info("sbertask: assigned major number %d\n", major_number);

	/* Cache create */
	queue_cache = kmem_cache_create("sbertask_queue", sizeof(struct queue_element), 0, SLAB_HWCACHE_ALIGN, NULL);
	if (queue_cache == NULL){
		pr_err("sbertask: can't create queue cache\n");
		unregister_chrdev(major_number, DEVICE_NAME);
		return 2;
	}
	pr_info("sbertask: module successfully loaded\n");
	return 0;


};

static void __exit module_stop(void)
{
	int count = 0;
	struct rb_node *node;
	for (node = rb_first(&root); node; node = rb_next(node)){
		struct rb_buf_node *selected_buf_node;
		pr_info("clearing rb tree node %u\n", count++);
                selected_buf_node = container_of( node, struct rb_buf_node, node);
		pr_info("deleted buffer for pid %u\n", selected_buf_node->pid);
		rm_buffer(selected_buf_node->pid);	
	}
	kmem_cache_destroy(queue_cache);
	unregister_chrdev(major_number, DEVICE_NAME);
	pr_info("sbertask: module unloaded\n");
};

module_init(module_start);
module_exit(module_stop);



