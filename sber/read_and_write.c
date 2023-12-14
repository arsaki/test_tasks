#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUF_SIZE 1000
void main (void){
	const void *s = "1234567890\n";
	void *d;
	int fd;
	int res;
	fd = open("/dev/sbertask", O_CREAT | O_TRUNC | O_RDWR, 0666);
	res = write(fd, s, strlen(s));
	lseek(fd,0,0);
	d = malloc(BUF_SIZE);
	res = read(fd, d, BUF_SIZE);
	close(fd);
	printf("%s\n", d);
	free(d);
}
