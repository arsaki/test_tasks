/*
 * 	Test task by SBER
 *
 *
 * 	Symbol driver, works as FIFO buffer via char device. 
 * 	Queue uses "struct list_head". Queue depth = 1000 байт.
 * 	
 * 	Driver modes:
 *	
 *	*Default  - one buffer,	multiple access
 *	*Single   - one buffer, single access
 * *	*Multiple - multiple buffers, multiple access
 *	
 *	Driver accept binary data.
 * 	Driver has error(like overflow) and diagnostic messages
 * 	Linux API comatible
 *
 *	AUTHOR: Arsenii Akimov 
 *	DATE:   23/11/2023
 *	E-MAIL: arseniumfrela@bk.ru
 *	PHONE:  89055787819
 *	SOURCE: https://www.github.com/arsaki/test_tasks
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h> 
#include <linux/printk.h>

static char *mode_string = "default";

module_param(mode_string, charp, 0000);
MODULE_PARM_DESC(mode_string, "Select  mode: default/single/multiple");

static int __init module_start(void){
	pr_info("sbertask module successfully loaded");
	return 0;
}

static void __exit module_stop(void){
	pr_info("sbertask module unloaded");
}

module_init(module_start);
module_exit(module_stop);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arsenii Akimov <arseniumfrela@bk.ru>");
MODULE_DESCRIPTION("FIFO buffer driver. Runs 3 modes: default, single, multiple");

