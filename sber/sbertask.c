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
#include <linux/mutex.h>
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

DEFINE_MUTEX(read_mutex);
DEFINE_SPINLOCK(queue_lock);
DEFINE_SPINLOCK(rb_tree_lock);

static int add_buffer(pid_t pid)
{
	struct rb_buf_node *new_rb_buf_node;
	struct rb_node **select_node = &(root.rb_node); 
        struct rb_node *parent = NULL;

	new_rb_buf_node = kmalloc(sizeof(struct rb_buf_node), GFP_KERNEL);

	spin_lock(&rb_tree_lock);

	/* Sliding on tree */
	while (*select_node) {
		struct rb_buf_node *selected_buf_node;
	        selected_buf_node = container_of(*select_node, struct rb_buf_node, node);
		if (pid < selected_buf_node->pid)
			select_node = &((*select_node)->rb_left);
	      	else if (pid > selected_buf_node->pid)
		      	select_node = &((*select_node)->rb_right);
	      	else{
			kfree(new_rb_buf_node);
		      	return 1;
		}
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&new_rb_buf_node->node, parent, select_node);
	rb_insert_color(&new_rb_buf_node->node, &root);

	spin_unlock(&rb_tree_lock);

	return 0;
}

static struct rb_buf_node *get_buffer(pid_t pid)
{
	struct rb_node **select_node = &(root.rb_node); 
	struct rb_buf_node * selected_buf_node = container_of(*select_node, struct rb_buf_node, node);

	spin_lock(&rb_tree_lock);

	while (selected_buf_node->pid) {
                struct rb_buf_node *selected_buf_node;
                selected_buf_node = container_of(*select_node, struct rb_buf_node, node);
                if (pid < selected_buf_node->pid)
                        select_node = &((*select_node)->rb_left);
                else if (pid > selected_buf_node->pid)
                        select_node = &((*select_node)->rb_right);
        
	}

	spin_unlock(&rb_tree_lock);
	return NULL;
}

static int rm_buffer(pid_t pid)
{
	struct rb_buf_node *rm_buf;
        struct queue_element *queue_entry, *queue_next;
      	int i;

	spin_lock(&rb_tree_lock);

	rm_buf = get_buffer(pid);
 	if (rm_buf->queue_head != NULL)
                list_for_each_entry_safe(queue_entry, queue_next, &rm_buf->queue_head->list, list){
                        kmem_cache_free(queue_cache, queue_entry);
                        pr_info("sbertask: pid %u, queue counter cache free %i", current->pid, i);
                }
	rb_erase(&rm_buf->node, &root);
	kfree(&rm_buf);

	spin_unlock(&rb_tree_lock);
	return 0;
}




static int sbertask_open (struct inode *inode, struct file *file_p)
{
	try_module_get(THIS_MODULE);
	/* Buffer create */
	if (!add_buffer(current->pid))
		pr_info("sbertask: process with pid %u opened device\n", current->pid);

	/* Mutex */
	mutex_init(&read_mutex);
	if (!mutex_lock_interruptible(&read_mutex))
	pr_info("sbertask: device %s opened\n", DEVICE_NAME);
	return 0;
};

static  ssize_t sbertask_read (struct file *file_p, char __user *buf, size_t length, loff_t *off_p)
{		
	struct rb_buf_node *tmp_buf_node;
	struct queue_element *queue_head, *queue_tail, *queue_prev;
	pr_info("sbertask: process with pid %u read device\n", current->pid);	
	tmp_buf_node = get_buffer(current->pid);  
	queue_head = tmp_buf_node->queue_head;	
	queue_tail = tmp_buf_node->queue_tail;	

	if(!queue_head){
		pr_info("sbertask: queue is empty for process with pid %u\n", current->pid);
		/* lock reading with rw semaphore */
	}
	spin_lock(&queue_lock);
	if(put_user(queue_head->data, buf)){
		spin_unlock(&queue_lock);
		pr_err("sbertask: can't put data to userspace!\n");
		return -EINVAL;
	}
	pr_info("sbertask: sended char %c\n", queue_head->data);
	/* time to delete entry */
	if(queue_head->list.next != &queue_head->list){
		queue_prev = queue_head;
		queue_head = list_entry(queue_head->list.next, struct queue_element, list);
		list_del(&queue_prev->list);
		kmem_cache_free(queue_cache, queue_prev);
	} else
		kmem_cache_free(queue_cache, queue_head);
	spin_unlock(&queue_lock);
	return 1;
};

static	ssize_t sbertask_write (struct file *file_p, const char __user *buf, size_t length, loff_t *off_p)
{
	struct rb_buf_node * tmp_buf_node;
	struct queue_element *queue_head, *queue_tail;
	int queue_length;
	tmp_buf_node = get_buffer(current->pid);
	queue_head = tmp_buf_node->queue_head;
	queue_tail = tmp_buf_node->queue_tail;
	queue_length = tmp_buf_node->queue_length;

	pr_info("sbertask: process with pid %u write device\n", current->pid);	
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
        if (rm_buffer(current->pid))
                pr_info("sbertask: process with pid %u closed device\n", current->pid);
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
	pr_info("sbertask: mode %s\n", mode_string);
	
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
	struct rb_node *node;
	for (node = rb_first(&root); node; node = rb_next(node)){
		struct rb_buf_node *selected_buf_node;
                selected_buf_node = container_of( node, struct rb_buf_node, node);
		rm_buffer(selected_buf_node->pid);	
	}
	kmem_cache_destroy(queue_cache);
	unregister_chrdev(major_number, DEVICE_NAME);
	pr_info("sbertask: module unloaded\n");
};

module_init(module_start);
module_exit(module_stop);



