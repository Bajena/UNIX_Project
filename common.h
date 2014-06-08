#ifndef common_h
#define common_h

#include <netinet/in.h>

#define ERR(source) (perror(source),\
         fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
         exit(EXIT_FAILURE))

#define BUFFER_SIZE 512
#define MAX_ATTEMPTS 3
#define CONFIRM_TIME 500000

#define POSITION_REQUEST_MESSAGE '0'
#define POSITION_RESPONSE_MESSAGE '1'
#define REGISTER_VEHICLE_REQUEST_MESSAGE '2'
#define REGISTER_VEHICLE_RESPONSE_MESSAGE '3'
#define UNREGISTER_VEHICLE_REQUEST_MESSAGE '4'
#define UNREGISTER_VEHICLE_RESPONSE_MESSAGE '5'
#define VEHICLE_HISTORY_REQUEST_MESSAGE '6'
#define VEHICLE_HISTORY_RESPONSE_START_MESSAGE '7'
#define VEHICLE_HISTORY_RESPONSE_DATA_MESSAGE '8'
#define VEHICLE_HISTORY_RESPONSE_END_MESSAGE '9'
#define CALCULATE_LONGEST_ROAD_REQUEST_MESSAGE 'A'
#define CALCULATE_LONGEST_ROAD_RESPONSE_MESSAGE 'B'
#define CALCULATIONS_STATUS_REQUEST 'C'
#define CALCULATIONS_STATUS_RESPONSE 'D'
#define CHECK_REQUEST 'E'

struct message {
      int type;
      char *text;
      struct sockaddr_in *addr;
};


typedef struct Position
{
	int x;
	int y;
} Position;

void sleep_nanoseconds(unsigned long nanoseconds);
int make_socket(void);
void* mymalloc (size_t size);
int sethandler( void (*f)(int), int sigNo);
void set_alarm(int sec, int usec);
void cancel_alarm();
int send_datagram(int sock,struct sockaddr_in *addr, char type,char *text);
struct message* recv_datagram(int sock);
struct message* create_message(char type, char *text, struct sockaddr_in *addr) ;
void destroy_message(struct message* msg);
ssize_t bulk_read(int fd, char *buf, size_t count);
ssize_t bulk_read_line(int fd, char *buf, size_t count);
ssize_t bulk_write(int fd, char *buf, size_t count);

#endif
