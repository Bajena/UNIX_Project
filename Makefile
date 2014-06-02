all: vehicle server client
vehicle: vehicle.c
	gcc -Wall -o vehicle vehicle.c common.c -lpthread
server: server.c
	gcc -Wall -o server server.c common.c vector.c -lpthread
client: client.c
	gcc -Wall -o client client.c common.c -lpthread
.PHONY: clean
clean:
	-rm vehicle server client
