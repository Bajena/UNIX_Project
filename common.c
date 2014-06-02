#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include "common.h"

void sleep_nanoseconds(unsigned long nanoseconds)
{
			unsigned long nsec = nanoseconds / 1000000000;
			unsigned long nnanosec = nanoseconds % 1000000000;
	struct timespec tt, t = {nsec,nnanosec};
	for (tt = t;nanosleep(&tt,&tt);)
		if (errno!=EINTR) ERR("nanosleep:");
}

int make_socket(void){
	int sock;
	sock = socket(PF_INET,SOCK_DGRAM,0);
	if(sock < 0) ERR("socket");
	return sock;
}

void* mymalloc (size_t size) {
	void* result = malloc(size);
	if (result==NULL) ERR("malloc");
	return result;
}

int sethandler( void (*f)(int), int sigNo) {
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1==sigaction(sigNo, &act, NULL))
		return -1;
	return 0;
}

void set_alarm(int sec, int usec) {
	struct itimerval tv;
	memset(&tv, 0, sizeof(struct itimerval));
	tv.it_value.tv_sec = sec;
	tv.it_value.tv_usec=usec;

	setitimer(ITIMER_REAL,&tv,NULL);
}

void cancel_alarm() {
	set_alarm(0,0);
}

int send_datagram(int sock,struct sockaddr_in *addr, char type,char *text){
	int status;
	char msg[BUFFER_SIZE];
      snprintf(msg,BUFFER_SIZE,"%c%s",type,text);
	status=TEMP_FAILURE_RETRY(sendto(sock,msg,strlen(msg),0,(struct sockaddr*)addr,sizeof(*addr)));
	if(status<0&&errno!=EPIPE&&errno!=ECONNRESET) ERR("sendto");
	return status;
}

struct message* recv_datagram(int sock){
	char buf[BUFFER_SIZE];
	int len;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	if((len = recvfrom(sock,buf,BUFFER_SIZE,0,(struct sockaddr*) &addr,&addrlen))<0) {
		if(EINTR!=errno)ERR("recvfrom"); }
	buf[len] = '\0';
	return  create_message(buf[0], buf+1, &addr);
}
struct message* create_message(char type, char *text, struct sockaddr_in *addr) {
	 if (strlen(text) == 0) return NULL;
	 struct message *msg;
	 struct sockaddr_in *addr_clone;
	 msg =  (struct message*)mymalloc(sizeof(struct message));
	 msg->type = type;
	 msg->text =  (char*)mymalloc(strlen(text));
	 strcpy(msg->text,text);

	 if (addr!=NULL){
		addr_clone = (struct sockaddr_in* )mymalloc(sizeof(struct sockaddr_in ));
		*addr_clone = *addr;
		msg->addr = addr_clone;
		}
	 return msg;
}

void destroy_message(struct message* msg) {
	if (msg!=NULL) {
		free(msg->text);
		free(msg->addr);
		free(msg);
	}
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
	int c;
	size_t len = 0;

	do
	{
		c = TEMP_FAILURE_RETRY(read(fd, buf, count));
		if (c < 0)
			return c;
		if (c == 0)
			return len;
		buf += c;
		len += c;
		count -= c;
	}
	while (count > 0);

	return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
	int c;
	size_t len = 0;

	do
	{
		c = TEMP_FAILURE_RETRY(write(fd, buf, count));
		if(c < 0)
			return c;
		buf += c;
		len += c;
		count -= c;
	}
	while (count > 0);

	return len;
}
