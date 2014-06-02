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
#include <signal.h>
#include <netdb.h>

#include "common.h"


struct sockaddr_in make_address(char *address, int port){
	struct sockaddr_in addr;
	struct hostent *hostinfo;
	addr.sin_family = AF_INET;
	addr.sin_port = htons (port);
	hostinfo = gethostbyname(address);
	if(hostinfo == NULL)ERR("gethost:");
	addr.sin_addr = *(struct in_addr*) hostinfo->h_addr;
	return addr;
}

void usage(char * name){
	fprintf(stderr,"USAGE: %s ADDRESS PORT\n",name);
}

int choose_option() {
	int selected_option = -1;
	do {
		fprintf(stderr, "WYBIERZ FUNKCJE:\n1 - Zarejestruj pojazd\n2 - Wyrejestruj pojazd\n3- Pobierz historie pojazdu\n4 - Oblicz ktory pojazd przejechal najdluzsza trase\n5 - Sprawdz status obliczen\n");
		fprintf(stderr,"Twoj wybor: ");

  		scanf ("%d",&selected_option);
	}
	while (selected_option <= 0 && selected_option >= 6) ;

	return selected_option;
}

void register_vehicle(int sfd, struct sockaddr_in *addr) {
	struct message *in_msg;

	char ipaddress[20];
	char fulladdress[25];
	int port;

	fprintf(stderr,"Podaj ip pojazdu: ");
  	scanf ("%s",ipaddress);
	fprintf(stderr,"Podaj port pojazdu: ");
  	scanf ("%d",&port);

	snprintf(fulladdress,25,"%s:%d",ipaddress,port);
	send_datagram(sfd,addr,REGISTER_VEHICLE_REQUEST_MESSAGE,fulladdress);

	in_msg = recv_datagram(sfd);
	if (in_msg==NULL) return;
	if (in_msg->type == REGISTER_VEHICLE_RESPONSE_MESSAGE) {
		fprintf(stderr,"Zarejestrowano pojazd o id: %s\n",in_msg->text);
	}
}

void work(int sfd, struct sockaddr_in *addr) {
	fprintf(stderr,"Klient administracyjny dziala...\n");
	for (;;){
		switch(choose_option()) {
			case 1:
				register_vehicle(sfd,addr);
				break;
		}
	}
}

int main(int argc, char** argv) {
	int sock;
	if (argc != 3) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	sock=make_socket();
	struct sockaddr_in addr;
	addr = make_address(argv[1],(short)atoi(argv[2]));

	//if(sethandler(sig_alrm,SIGALRM)) ERR("Seting SIGALRM:");
	work(sock, &addr);

	if(TEMP_FAILURE_RETRY(close(sock))<0)ERR("close");
	return EXIT_SUCCESS;
}


