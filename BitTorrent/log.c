#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "log.h"

int logfile_fd = -1;

void logcmd(char *fmt,...)
{
	va_list ap;

	va_start(ap,fmt);
	vprintf(fmt,ap);
	va_end(ap);
}

int init_logfile(char *filename)
{
	logfile_fd = open(filename,O_RDWR|O_CREAT|O_APPEND,0666);
	if(logfile_fd < 0) {
		printf("open logfile failed\n");
		return -1;
	}

	return 0;
}

int logfile(char *file,int line,char *msg)
{
	char buff[256];

	if(logfile_fd < 0)  return -1;

	snprintf(buff,256,"%s:%d %s\n",file,line,msg);
	write(logfile_fd,buff,strlen(buff));
	
	return 0;
}
