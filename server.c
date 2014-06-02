#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include "common.h"
#include "vector.h"

#define POSITION_REFRESH_TIME 1500000000

volatile sig_atomic_t last_signal=0;

static pthread_mutex_t vehicles_mutex =  PTHREAD_MUTEX_INITIALIZER;

typedef struct vehicle {
	int id;
	struct sockaddr_in addr;

} vehicle;

vector registered_vehicles;

void sig_alrm(int i) {
	last_signal=i;
}

int bind_socket(int port){
	int sock = make_socket();
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons (port);
	addr.sin_addr.s_addr = htonl (INADDR_ANY);
	if(bind(sock,(struct sockaddr*) &addr,sizeof(struct sockaddr_in)) < 0) ERR("bind");
	fprintf(stderr, "Serwer uruchomiony...\n");
	return sock;
}

void usage(char * name){
	fprintf(stderr,"USAGE: %s PORT \n",name);
}

int send_and_confirm(int sock, struct sockaddr_in *addr, char type, char* text) {
	int attempts = 0;
	char recv_buffer[BUFFER_SIZE];
	for (attempts = 0; attempts< MAX_ATTEMPTS; attempts++) {
		last_signal = 0;
		set_alarm(0,CONFIRM_TIME);
		if(send_datagram(sock,addr,type, text)<0){
			fprintf(stderr,"Błąd wysyłania, probuję ponownie...\n");
			continue;
		}
		if (recv(sock,recv_buffer,BUFFER_SIZE,0)<0){
			if(EINTR!=errno)ERR("recv:");
			if(last_signal==SIGALRM) {
				fprintf(stderr, "Timeout, wysylam ponownie\n");
				continue;
			}
		}
		return 0;
	}
	return -1;
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

struct sockaddr_in make_address2(char *fulladdress){
	int i;
	char ipaddress[20];
	int port;
	for (i = 0;i<strlen(fulladdress);i++) {
		if (fulladdress[i]==':') {
			snprintf(ipaddress,i+1,"%s",fulladdress);
			port = atoi(fulladdress + i + 1);
		}
	}
	fprintf(stderr, "FullAdrres: %s\nIP: %s\nPort: %d\n", fulladdress,ipaddress,port);
	return make_address(ipaddress,port);
}


void register_new_vehicle(int sfd, struct sockaddr_in* client_addr, char* vehicle_address) {

	int vehicles_count = vector_length(&registered_vehicles);
	int assigned_id = 1;

	if (vehicles_count>0) {
		vehicle last_vehicle;
		vector_get(&registered_vehicles,vector_length(&registered_vehicles)-1,&last_vehicle);
		assigned_id = last_vehicle.id + 1;
	}

	vehicle new_vehicle;
	new_vehicle.addr = make_address2(vehicle_address);
	new_vehicle.id = assigned_id;

	vector_push(&registered_vehicles,&new_vehicle);

	fprintf(stderr,"Pojazd zarejestrowany pod adresem:%s:%d\n",inet_ntoa(new_vehicle.addr.sin_addr),ntohs(new_vehicle.addr.sin_port));

	char id_string[4];
	snprintf(id_string,4,"%d",new_vehicle.id);
	send_datagram(sfd, client_addr, REGISTER_VEHICLE_RESPONSE_MESSAGE, id_string);
}

// Zwraca index w tablicy registered_vehicles na podstawie id
// Jeśli nie ma pojazdu o podanym id zwraca -1
int get_vehicle_by_id(int id) {
	vehicle current_vehicle;
	int i;
	int vehicles_count = vector_length(&registered_vehicles);
	for (i= 0;i<vehicles_count;i++) {
		vector_get(&registered_vehicles,i,&current_vehicle);
		if (current_vehicle.id==id)
			return i;
	}
	return -1;
}

// Zwraca index w tablicy registered_vehicles na podstawie adresu
// Jeśli nie ma pojazdu o podanym adresie zwraca -1
int get_vehicle_by_addr(struct sockaddr_in *addr) {
	vehicle current_vehicle;
	int i;
	int vehicles_count = vector_length(&registered_vehicles);
	for (i= 0;i<vehicles_count;i++) {
		vector_get(&registered_vehicles,i,&current_vehicle);
		if (current_vehicle.addr.sin_port==addr->sin_port &&  strcmp(inet_ntoa(current_vehicle.addr.sin_addr),inet_ntoa(addr->sin_addr))==0)
			return i;
	}
	return -1;
}


void unregister_vehicle(int sfd, struct message *in_msg) {
	int vehicle_index;
	int id_to_deregister = atoi(in_msg->text);

	if ((vehicle_index=get_vehicle_by_id(id_to_deregister))==-1) {
		send_datagram(sfd, in_msg->addr, UNREGISTER_VEHICLE_RESPONSE_MESSAGE, "Brak pojazdu o podanym id");
	}
	else {
		fprintf(stderr,"Odebrano zadanie wyrejestrowania pojazdu\nID: %d , index w tablicy: %d\n",id_to_deregister,vehicle_index);
		vector_remove(&registered_vehicles, vehicle_index);
		send_datagram(sfd, in_msg->addr, UNREGISTER_VEHICLE_RESPONSE_MESSAGE, "Wyrejestrowano pojazd o podanym id");
	}
}

void *request_vehicles_positions(void *sfd_void) {
	int sfd = *((int*)sfd_void);
	vehicle current_vehicle;
	int vehicles_count ;
	int i;

	for (;;) {
		if (pthread_mutex_lock(&vehicles_mutex)!=0) ERR("mutex_lock");
		vehicles_count = vector_length(&registered_vehicles);
		for (i= 0;i<vehicles_count;i++) {
			vector_get(&registered_vehicles,i,&current_vehicle);
			send_datagram(sfd, &current_vehicle.addr, POSITION_REQUEST_MESSAGE, " ");
		}

		if (pthread_mutex_unlock(&vehicles_mutex)!=0) ERR("mutex_unlock");

		sleep_nanoseconds(POSITION_REFRESH_TIME);
	}
	return NULL;
}

void process_vehicle_position(struct message *in_msg) {
	vehicle current_vehicle;
	int  vehicle_index = get_vehicle_by_addr(in_msg->addr);
	vector_get(&registered_vehicles,vehicle_index,&current_vehicle);

	fprintf(stderr,"[%d] Pozycja: %s\n",current_vehicle.id, in_msg->text);

}

void work(int sfd) {
	struct message *in_msg;
	vector_init(&registered_vehicles,sizeof(vehicle),0,NULL);

	pthread_t positions_thread;
 	if (pthread_create(&positions_thread, NULL, request_vehicles_positions, &sfd)!=0)ERR("create_thread");

	for (;;) {
		in_msg = recv_datagram(sfd);
		if (in_msg==NULL) continue;
		if (in_msg->type == REGISTER_VEHICLE_REQUEST_MESSAGE) {
			if (pthread_mutex_lock(&vehicles_mutex)!=0) ERR("mutex_lock");
			register_new_vehicle(sfd,in_msg->addr, in_msg->text);
			if (pthread_mutex_unlock(&vehicles_mutex)!=0) ERR("mutex_unlock");
		}
		else if (in_msg->type == UNREGISTER_VEHICLE_REQUEST_MESSAGE) {
			if (pthread_mutex_lock(&vehicles_mutex)!=0) ERR("mutex_lock");
			unregister_vehicle(sfd, in_msg);
			if (pthread_mutex_unlock(&vehicles_mutex)!=0) ERR("mutex_unlock");
		}
		else if (in_msg->type == POSITION_RESPONSE_MESSAGE) {
			if (pthread_mutex_lock(&vehicles_mutex)!=0) ERR("mutex_lock");
			process_vehicle_position(in_msg);
			if (pthread_mutex_unlock(&vehicles_mutex)!=0) ERR("mutex_unlock");
		}
		destroy_message(in_msg);
	}

 	if(pthread_join(positions_thread, NULL)) {
		ERR("join_thread");
	}

	vector_dispose(&registered_vehicles);
}

int main(int argc, char** argv) {
	int sfd ;

	if(argc!=2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	srand(time(NULL));
	if(sethandler(sig_alrm,SIGALRM)) ERR("Seting SIGALRM:");

	sfd=bind_socket(atoi(argv[1]));
	//work(sock, task_length,points_to_win);

	work(sfd);

	if(TEMP_FAILURE_RETRY(close(sfd))<0)ERR("close");
	return EXIT_SUCCESS;
}


