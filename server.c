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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include "common.h"
#include "vector.h"

#define POSITION_REFRESH_TIME 1500000000
#define FILE_SEGMENT_SIZE BUFFER_SIZE/2
#define LINE_BUFFER_SIZE 100
#define LOGS_DIRECTORY "logs"
#define LOG_FILE_PREFIX "log_"

typedef struct vehicle {
	int id;
	int log_fd;
	struct sockaddr_in addr;
	int unanswered_position_requests;
	pthread_mutex_t* log_mutex;
} vehicle;

typedef struct calculations_status {
	vehicle* curr_vehicle;

	int max_vehicle_id;
	int in_progess;
	double max_distance;
} calculations_status;

volatile sig_atomic_t last_signal=0;
static pthread_mutex_t vehicles_mutex =  PTHREAD_MUTEX_INITIALIZER;
calculations_status* active_calculation;

vector registered_vehicles;

void cleanup() {
	int vehicles_count = vector_length(&registered_vehicles);
	vehicle current_vehicle;
	if (active_calculation!=NULL) free(active_calculation);
	int i = 0;
	for (;i<vehicles_count;i++){
		vector_get(&registered_vehicles,i,&current_vehicle);
		fprintf(stderr, "Zamykam plik: %d\n", current_vehicle.log_fd);
		if(TEMP_FAILURE_RETRY(close(current_vehicle.log_fd))<0)ERR("close");
		if(TEMP_FAILURE_RETRY(pthread_mutex_destroy(current_vehicle.log_mutex))<0)ERR("close");
		free(current_vehicle.log_mutex);
	}

	vector_dispose(&registered_vehicles);
}

void sig_alrm(int i) {
	last_signal=i;
}

void sig_int(int signo) {
	last_signal = signo;
	fprintf(stderr,"\nSIGINT - sprzatam...\n");
	cleanup();
	exit(EXIT_SUCCESS);
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

// Sprawdza czy mozna wysylac wiadomosci na podany przez kleinta aders
int check_vehicle_valid(int sfd, vehicle* current_vehicle) {
	if (send_datagram(sfd, &current_vehicle->addr, CHECK_REQUEST, "test") < 0 && errno==22) return 0;
	return 1;
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
	new_vehicle.unanswered_position_requests = 0;

	// Sprawdz polaczenie


	//Plik logu
	char logname[20];
	snprintf(logname,20,"%s/%s%d",LOGS_DIRECTORY,LOG_FILE_PREFIX,new_vehicle.id);
	fprintf(stderr,"Tworze plik: %s\n",logname);
	int fd;
	if ((fd = open (logname, O_RDWR | O_CREAT | O_TRUNC | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO) )< 0)ERR("Create file");
	new_vehicle.log_fd = fd;

	new_vehicle.log_mutex = (pthread_mutex_t*) mymalloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(new_vehicle.log_mutex, NULL);

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

void remove_vehicle(vehicle current_vehicle) {
	if(TEMP_FAILURE_RETRY(close(current_vehicle.log_fd))<0)ERR("close");
	if(TEMP_FAILURE_RETRY(pthread_mutex_destroy(current_vehicle.log_mutex))<0)ERR("close");
	free(current_vehicle.log_mutex);

	int index = get_vehicle_by_id(current_vehicle.id);
	vector_remove(&registered_vehicles, index);
}

void update_vehicle(int id, vehicle *new_vehicle) {
	int index = get_vehicle_by_id(new_vehicle->id);
	vector_remove(&registered_vehicles, index);
	vector_insert_at(&registered_vehicles, new_vehicle, index);
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
			current_vehicle.unanswered_position_requests++;
			update_vehicle(current_vehicle.id, &current_vehicle);
			if (current_vehicle.unanswered_position_requests==MAX_ATTEMPTS) {
				fprintf(stderr,"Brak odpowiedzi od pojazdu %d - usuwam\n",current_vehicle.id);
				remove_vehicle(current_vehicle);
			}
			else
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
	current_vehicle.unanswered_position_requests = 0;

	update_vehicle(current_vehicle.id, &current_vehicle);
	char buffer[20];
	snprintf(buffer,20,"%s\n",in_msg->text);


	if (pthread_mutex_lock(current_vehicle.log_mutex)!=0) ERR("mutex_lock");
	bulk_write(current_vehicle.log_fd, buffer, strlen(buffer));
	if (pthread_mutex_unlock(current_vehicle.log_mutex)!=0) ERR("mutex_unlock");
}

void send_vehicle_history(int sfd, struct message *in_msg) {
	vehicle current_vehicle;
	int vehicle_id = atoi(in_msg->text);
	int  vehicle_index = get_vehicle_by_id(vehicle_id);

	if (vehicle_index==-1){
		send_datagram(sfd, in_msg->addr, VEHICLE_HISTORY_RESPONSE_END_MESSAGE, "Brak pojazdu o podanym id");
		return;
	}

	vector_get(&registered_vehicles,vehicle_index,&current_vehicle);

	char buffer[FILE_SEGMENT_SIZE];
	send_datagram(sfd, in_msg->addr, VEHICLE_HISTORY_RESPONSE_START_MESSAGE, "Reading");
	if (pthread_mutex_lock(current_vehicle.log_mutex)!=0) ERR("mutex_lock");
	fprintf(stderr,"Pobieram historie pojazdu %d (fd=%d)...\n",current_vehicle.id, current_vehicle.log_fd);
	if (lseek(current_vehicle.log_fd, 0,SEEK_SET) < 0)ERR("lseek");
	int read_chars = 0;

	while ((read_chars = bulk_read(current_vehicle.log_fd, buffer, FILE_SEGMENT_SIZE)) > 0){
		buffer[read_chars] = '\0';
		fprintf(stderr,"%s",buffer);
		if (send_and_confirm(sfd, in_msg->addr, VEHICLE_HISTORY_RESPONSE_DATA_MESSAGE, buffer)==-1) {
			fprintf(stderr, "Blad podczas wysylania historii pojazdu - brak odpowiedzi ze strony klienta\n");
			if (lseek(current_vehicle.log_fd, SEEK_END,SEEK_SET) < 0)ERR("lseek");
			if (pthread_mutex_unlock(current_vehicle.log_mutex)!=0) ERR("mutex_unlock");
			return;
		}

		//send_datagram(sfd, in_msg->addr, VEHICLE_HISTORY_RESPONSE_DATA_MESSAGE, buffer);
	}
	if (lseek(current_vehicle.log_fd, SEEK_END,SEEK_SET) < 0)ERR("lseek");

	send_datagram(sfd, in_msg->addr, VEHICLE_HISTORY_RESPONSE_END_MESSAGE, "Done");
	fprintf(stderr,"\nWysylanie zakonczone\n");
	if (pthread_mutex_unlock(current_vehicle.log_mutex)!=0) ERR("mutex_unlock");
}

Position string_to_point(char* line){
	Position pos;
	int i;
	int num_index = 0;

	char num_buffer[10];
	for (i = 0;i<strlen(line);i++){
		if (line[i]=='('){
		}
		else if (line[i]==',') {
			num_buffer[num_index] = '\0';
			pos.x = atoi(num_buffer);
			fprintf(stderr, "X = %s\n",num_buffer);
			num_buffer[0] = '\0';
			num_index = 0;
		}
		else if (line[i]==')') {
			num_buffer[num_index] = '\0';
			pos.y = atoi(num_buffer);
			fprintf(stderr, "Y = %s\n",num_buffer);
			break;
		}
		else num_buffer[num_index++] = line[i];
	}

	return pos;
}

double calculate_vehicle_road(vehicle *current_vehicle){
	vehicle* vehicle = current_vehicle;

	if (pthread_mutex_lock(vehicle->log_mutex)!=0) ERR("mutex_lock");
	char buffer[LINE_BUFFER_SIZE];
	int read_chars = 0;
	Position last_pos, curr_pos;
	last_pos.x = -1;
	last_pos.y = -1;
	fprintf(stderr,"Pojazd: %d\n",vehicle->id);
	double distance = 0.0;

	if (lseek(vehicle->log_fd, 0,SEEK_SET) < 0)ERR("lseek");
	while ((read_chars = bulk_read_line(vehicle->log_fd,buffer,LINE_BUFFER_SIZE)) > 0) {
		buffer[read_chars] = '\0';
		if (last_pos.x==-1 && last_pos.y==-1){
			curr_pos = string_to_point(buffer);
			last_pos = curr_pos;
		}
		else {
			last_pos = curr_pos;
			curr_pos = string_to_point(buffer);
		}
		double last_distance = sqrt(pow(curr_pos.x-last_pos.x,2) + pow(curr_pos.y - last_pos.y,2));
		distance+=last_distance;
		fprintf(stderr,"Pozycja: (%d,%d)\nOdcinek: %.2f\nLaczny dystans: %.2f\n",curr_pos.x,curr_pos.y, last_distance, distance);

	}
	if (lseek(vehicle->log_fd, SEEK_END,SEEK_SET) < 0)ERR("lseek");

	if (pthread_mutex_unlock(vehicle->log_mutex)!=0) ERR("mutex_unlock");

	return distance;
}

void *perform_longest_road_calculations(void *param) {
	if (active_calculation!=NULL) free(active_calculation);
	active_calculation = (calculations_status*) mymalloc(sizeof(calculations_status));
 	active_calculation->in_progess = 1;
	vector vehicles_clone;
	vector_init(&vehicles_clone,sizeof(vehicle),0,NULL);

	if (pthread_mutex_lock(&vehicles_mutex)!=0) ERR("mutex_lock");
	vector_copy(&registered_vehicles,&vehicles_clone);
	if (pthread_mutex_unlock(&vehicles_mutex)!=0) ERR("mutex_unlock");

	int i;
	vehicle current_vehicle;
	int vehicles_count = vector_length(&vehicles_clone);
	for (i= 0;i<vehicles_count;i++) {
		vector_get(&vehicles_clone,i,&current_vehicle);
		active_calculation->curr_vehicle = &current_vehicle;
		double vehicle_distance = calculate_vehicle_road(&current_vehicle);
		if (vehicle_distance > active_calculation->max_distance) {
			active_calculation->max_vehicle_id = current_vehicle.id;
			active_calculation->max_distance = vehicle_distance;
		}
	}

	active_calculation->in_progess = 0;
	fprintf(stderr,"Najwieksza trase: %.2f pokonal pojazd o id: %d\n",active_calculation->max_distance, active_calculation->max_vehicle_id);
	return NULL;
}

void begin_longest_road_calculations(int sfd, struct message *in_msg) {
	if (active_calculation!=NULL && active_calculation->in_progess) {
		send_datagram(sfd, in_msg->addr, CALCULATE_LONGEST_ROAD_RESPONSE_MESSAGE, "Nie mozna rozpoczac liczenia - trwa inne obliczenie!");
		return;
	}

	if (vector_length(&registered_vehicles)==0) {
		send_datagram(sfd, in_msg->addr, CALCULATE_LONGEST_ROAD_RESPONSE_MESSAGE, "Nie mozna rozpoczac liczenia - nie zarejestrowano zadnych pojazdow!");
		return;
	}

	pthread_t calculations_thread;
 	if (pthread_create(&calculations_thread, NULL, perform_longest_road_calculations, NULL)!=0)ERR("create_thread");
	send_datagram(sfd, in_msg->addr, CALCULATE_LONGEST_ROAD_RESPONSE_MESSAGE, "Rozpoczeto obliczenia...");
	fprintf(stderr,"Ropoczeto obliczenia najdluzszej drogi...\n");
}

void send_calculations_status(int sfd, struct message *in_msg) {
	char response_message[BUFFER_SIZE];
	if (active_calculation==NULL) {
		snprintf(response_message, BUFFER_SIZE, "Aktualnie nie trwa zadne obliczenie!");
	}
	else if (active_calculation->in_progess==1) {
		snprintf(response_message, BUFFER_SIZE, "Obliczenia w toku...");
	}
	else if (active_calculation->in_progess == 0) {
		snprintf(response_message, BUFFER_SIZE, "Obliczenia zakonczone.\nNajdluzsza trasa: %.2f\nPojazd: %d", active_calculation->max_distance, active_calculation->max_vehicle_id);
	}

	send_datagram(sfd, in_msg->addr, CALCULATIONS_STATUS_RESPONSE, response_message);
	fprintf(stderr,"Wyslano status obliczen\n");
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
		else if (in_msg->type==VEHICLE_HISTORY_REQUEST_MESSAGE) {
			send_vehicle_history(sfd,in_msg);
		}
		else if (in_msg->type==CALCULATE_LONGEST_ROAD_REQUEST_MESSAGE) {
			begin_longest_road_calculations(sfd,in_msg);
		}
		else if (in_msg->type==CALCULATIONS_STATUS_REQUEST) {
			send_calculations_status(sfd,in_msg);
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
	if(sethandler(sig_int,SIGINT)) ERR("Seting SIGINT:");

	sfd=bind_socket(atoi(argv[1]));

	int mkdir_result;
	if ((mkdir_result = mkdir(LOGS_DIRECTORY, 0777)) < 0) {
		if (mkdir_result!=-1)
			fprintf(stderr, "Error:%d",mkdir_result);
	}
	//work(sock, task_length,points_to_win);

	work(sfd);

	cleanup();
	if(TEMP_FAILURE_RETRY(close(sfd))<0)ERR("close");
	return EXIT_SUCCESS;
}


