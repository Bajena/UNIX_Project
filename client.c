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


volatile sig_atomic_t last_signal=0;


void sig_alrm(int i) {
	last_signal=i;
}

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


struct message* timed_recv(int sock) {
	struct message* in_msg;
	set_alarm(0,CONFIRM_TIME);
	if (((in_msg=recv_datagram(sock))==NULL) && errno==EINTR){
		if(last_signal==SIGALRM) {
			fprintf(stderr, "Timeout podczas odbierania wiadomosci\n");
			return NULL;
		}
	}
	cancel_alarm();
	return in_msg;
}

void usage(char * name){
	fprintf(stderr,"USAGE: %s ADDRESS PORT\n",name);
}

int choose_option() {
	int selected_option = -1;
	do {
		fprintf(stderr, "WYBIERZ FUNKCJE:\n1 - Zarejestruj pojazd\n2 - Wyrejestruj pojazd\n3 - Pobierz historie pojazdu\n4 - Oblicz ktory pojazd przejechal najdluzsza trase\n5 - Sprawdz status obliczen\n");
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

	in_msg = timed_recv(sfd);
	if (in_msg==NULL) {
		 fprintf(stderr, "Brak odpowiedzi od serwera\n");
		 return;
	}
	if (in_msg->type == REGISTER_VEHICLE_RESPONSE_MESSAGE) {
		fprintf(stderr,"Zarejestrowano pojazd o id: %s\n",in_msg->text);
	}

	destroy_message(in_msg);
}

void unregister_vehicle(int sfd,struct sockaddr_in *addr) {
	int vehicle_id;
	char unregister_message_text[4];

	struct message *in_msg;

	fprintf(stderr,"Podaj ID pojazdu: ");
  	scanf ("%d",&vehicle_id);

	snprintf(unregister_message_text,4,"%d",vehicle_id);

	send_datagram(sfd,addr,UNREGISTER_VEHICLE_REQUEST_MESSAGE,unregister_message_text);

	in_msg = recv_datagram(sfd);
	if (in_msg==NULL) {
		 fprintf(stderr, "Brak odpowiedzi od serwera\n");
		 return;
	}
	if (in_msg->type == UNREGISTER_VEHICLE_RESPONSE_MESSAGE) {
		fprintf(stderr,"Odpowiedz serwera: %s\n",in_msg->text);
	}

	destroy_message(in_msg);
}

void get_vehicle_history(int sfd,struct sockaddr_in *addr) {
	int vehicle_id;
	char message_text[4];

	struct message *in_msg;

	fprintf(stderr,"Podaj ID pojazdu: ");
  	scanf ("%d",&vehicle_id);
	snprintf(message_text,4,"%d",vehicle_id);

	send_datagram(sfd,addr,VEHICLE_HISTORY_REQUEST_MESSAGE,message_text);
	int receiving_data = 0;

	do {
		receiving_data = 0;
		in_msg = timed_recv(sfd);
		if (in_msg==NULL) {
			 fprintf(stderr, "Brak odpowiedzi od serwera\n");
			 return;
		}
		if (in_msg->type == VEHICLE_HISTORY_RESPONSE_START_MESSAGE) {
			fprintf(stderr,"Historia pojazdu: \n");
			receiving_data = 1;
			send_datagram(sfd,addr, VEHICLE_HISTORY_RESPONSE_DATA_MESSAGE, "Received");
		}
		else if (in_msg->type == VEHICLE_HISTORY_RESPONSE_DATA_MESSAGE) {
			fprintf(stderr,"%s",in_msg->text);
			receiving_data = 1;
			send_datagram(sfd,addr, VEHICLE_HISTORY_RESPONSE_DATA_MESSAGE, "Received");
		}
		destroy_message(in_msg);
	}
	while (receiving_data==1);
}

void request_longest_road_calculation(int sfd,struct sockaddr_in *addr) {
	struct message *in_msg;

	send_datagram(sfd,addr,CALCULATE_LONGEST_ROAD_REQUEST_MESSAGE,"Pliz calculate da shit!");

	in_msg = timed_recv(sfd);
	if (in_msg==NULL) {
		 fprintf(stderr, "Brak odpowiedzi od serwera\n");
		 return;
	}
	else if (in_msg->type == CALCULATE_LONGEST_ROAD_RESPONSE_MESSAGE) {
		fprintf(stderr,"Odpowiedz serwera: %s\n",in_msg->text);
	}
	destroy_message(in_msg);
}

void request_calculations_status(int sfd,struct sockaddr_in *addr) {
	struct message *in_msg;

	send_datagram(sfd,addr,CALCULATIONS_STATUS_REQUEST,"Pliz calculate da shit!");

	in_msg = timed_recv(sfd);
	if (in_msg==NULL) {
		 fprintf(stderr, "Brak odpowiedzi od serwera\n");
		 return;
	}
	if (in_msg->type == CALCULATIONS_STATUS_RESPONSE) {
		fprintf(stderr,"Odpowiedz serwera: %s\n",in_msg->text);
	}
	destroy_message(in_msg);
}

void work(int sfd, struct sockaddr_in *addr) {
	fprintf(stderr,"Klient administracyjny dziala...\n");
	for (;;){
		switch(choose_option()) {
			case 1:
				register_vehicle(sfd,addr);
				break;
			case 2:
				unregister_vehicle(sfd,addr);
				break;
			case 3:
				get_vehicle_history(sfd,addr);
				break;
			case 4:
				request_longest_road_calculation(sfd,addr);
				break;
			case 5:
				request_calculations_status(sfd,addr);
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

	if(sethandler(sig_alrm,SIGALRM)) ERR("Seting SIGALRM:");
	work(sock, &addr);

	if(TEMP_FAILURE_RETRY(close(sock))<0)ERR("close");
	return EXIT_SUCCESS;
}


