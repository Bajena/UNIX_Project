#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <pthread.h>

#include "common.h"

#define MIN_X_GPS -9000
#define MAX_X_GPS 9000
#define MIN_Y_GPS -18000
#define MAX_Y_GPS 18000
#define MAX_MOVE_GPS 10
#define POSITION_CHANGE_TIME 100000000

volatile sig_atomic_t last_signal=0;

Position current_position;

void sig_alrm(int i){
	last_signal=i;
}

int bind_socket(int port){
	int sock = make_socket();
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons (port);
	addr.sin_addr.s_addr = htonl (INADDR_ANY);
	if(bind(sock,(struct sockaddr*) &addr,sizeof(struct sockaddr_in)) < 0) ERR("bind");
  	fprintf(stderr, "Pojazd pracuje pod adresem:%s:%d\n",inet_ntoa(addr.sin_addr),port);
	return sock;
}

void usage(char * name){
	fprintf(stderr,"USAGE: %s PORT\n",name);
}

void send_current_position(int sfd,struct sockaddr_in *addr){
	char position_string[20];

	snprintf(position_string,20,"(%d,%d)",current_position.x,current_position.y);
	send_datagram(sfd,addr, POSITION_RESPONSE_MESSAGE,position_string);
}
void process_requests(int sfd){
	struct message *in_msg;
	for (;;) {
		in_msg = recv_datagram(sfd);
		fprintf(stderr,"%c\n",in_msg->type);
		if (in_msg==NULL) continue;
		if (in_msg->type == POSITION_REQUEST_MESSAGE) {
			send_current_position(sfd,in_msg->addr);
		}
		destroy_message(in_msg);
	}
}

void* generate_path(void *arg)
{
	for(;;){
		int dx = rand()%MAX_MOVE_GPS;
		int dy = rand()%MAX_MOVE_GPS;

		dx = rand()%2 == 0 ? -dx : dx;
		dy = rand()%2 == 0 ? -dy : dy;

		current_position.x+=dx;
		current_position.y+=dy;

	    	fprintf(stderr,"Pozycja: (%d,%d)\nPrzesuniecie: (%d,%d)\n",current_position.x,current_position.y,dx,dy);
	    	sleep_nanoseconds(POSITION_CHANGE_TIME);
	}

    return NULL;
}

void work(int sfd) {
	//Wylosuj poczatkowa pozycje
	current_position.x = rand()%(MAX_X_GPS-MIN_X_GPS) + MIN_X_GPS;
	current_position.y = rand()%(MAX_Y_GPS-MIN_Y_GPS) + MIN_Y_GPS;
	//Stwórz wątek poruszający pojazdem
	pthread_t path_thread;
 	if (pthread_create(&path_thread, NULL, generate_path, NULL)!=0)ERR("create_thread");

 	process_requests(sfd);

 	if(pthread_join(path_thread, NULL)) {
		ERR("join_thread");
	}
}

int main(int argc, char** argv) {
	int sock;
	if (argc != 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	srand(time(NULL));
	sock=bind_socket(atoi(argv[1]));

	if(sethandler(sig_alrm,SIGALRM)) ERR("Seting SIGALRM:");
	work(sock);

	if(TEMP_FAILURE_RETRY(close(sock))<0)ERR("close");

	return EXIT_SUCCESS;
}


