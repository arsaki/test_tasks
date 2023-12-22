* WHATS IS IT?

This is FIFO buffer driver, constructed by list_head. Supports 3 mode

* HOW TO USE IT?

	* Default mode. Has 1 buffer, supports 1 read and multipe write threads. 
		Run "sudo ./start.sh". 
		Use "cat /dev/sbertask | md5sum" and "cat sbertask.ko > /dev/sbertask" in another terminal..
	* Single mode. Has 1 buffer, supports 1 read and 1 write threads. Run "sudo ./start_single.sh", then
		"./read_write /dev/sbertask". Check with "cat /dev/sbertask" in 2 terminal simultaneosly.
	* Multi mode. Has multiple buffers and processes. 
		Run "sudo ./start_multi.sh", then "./read_write" in simultaneosly opened terminals.
		See in "sudo dmesg -wT" info messages.

