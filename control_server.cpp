/* A simple server in the internet domain using TCP
   The port number is passed as an argument */


// El server de control de be ser un proxy de paquetes? Entiendo 
// 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "include/ShmQueue.h"




List<hardware_clients> hardware_client_table;


// Request Hash table - Add to control
unorderedmap<request_state, (request_id, admin_id)> unanswered_calls_table;


void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
     int sockfd, newsockfd, portno;
     int pid;
     socklen_t clilen;
     char buffer[256];
     int val = 1;
     struct sockaddr_in serv_addr, cli_addr;
     int n;


     if (argc < 2) {
         fprintf(stderr,"ERROR, no port provided\n");
         exit(1);
     }
     
     // Creo socket
     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) 
        error("ERROR opening socket");
     bzero((char *) &serv_addr, sizeof(serv_addr));
     portno = atoi(argv[1]);
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);

     // Configuro Socket
     setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
              error("ERROR on binding");
     listen(sockfd,5);
     clilen = sizeof(cli_addr);
     

     // Abro tantos sockets
     while(newsockfd = accept(sockfd, 
                 (struct sockaddr *) &cli_addr, 
                 &clilen)){

        if (newsockfd < 0) 
        error("ERROR on accept");
        pid = fork();
        
        // Acepto
        if(pid){
            // paddre, lo llevo a escuchar otro proceso 
            newsockfd = NULL;
            continue;
        }
        

        bzero(buffer,256);
        n = read(newsockfd,buffer,255);
        if (n < 0) error("ERROR reading from socket");
        printf("Here is the message: %s\n",buffer);
        n = write(newsockfd,"I got your message",18);
        if (n < 0) error("ERROR writing to socket");
        close(newsockfd);

        }

     close(sockfd);
     return 0; 
}



//// Unir con este código
/*

const char[] hardwareTable2String(List<hardware_clients> * hardware_client_table){
	for(auto start = Iterator::List<hardware_clients>(hardware_client_table); start++; start!=end.hardware_client_table)
		string buf;
		
		// Strcture hardware_clients atributes
		return buf.to_c_string;
}


// Creería que tiene que ser una función del tipo void *
void * hardware_handler(<hardware_client_socket>, Shm_Queue* shared_que){
	ShmQueue<cmd>
	
	// Connect to socket
	connect(hardware_client_socket)
	
	while(1)
		while(res = read(hardware_client_socket)> until <cmd_completion>{
			cmd.add(res)
		}
		
		while(res = white(hardware_client_socket)){
		
		}
}


// Receiving Thread
res = read(<message_socket>)
if(cmd.header.api_id.family = API_CONTROL){
	// Trabajo sobre los comandos de control
	if(cmd.header.api_id.minor = HELLO){
		thread{hardware_handler()}
	}
}else{ // Command packet
	// Create hardware packet from 
	unanswered_calls_table.add(cmd.body.cmd_id)
}



if(cmd.header.type= RESPONSE) // Response/Request{
	// Tipo de resultado
	res= unanswered_calls_table.delete(cmd.body.cmd_id)
}

struct Admin_client{
	uint32_t admin_id: // Ipv4?
	
}

unordered_map<Shd_queue, device_id> Cmd_queues;
List<Admin> admin_list;

// Controller Thread
cmd = read(<message_socket>)
switch(cmd.Header.family){
	case(CONTROL)
		// Ver que hacer con cada uno de las 
		switch(cmd.header.api_id.minor)
			case(CTRL_HELLO)
				// Add to Admin Table 
				// Check if request
	
				if(cmd.Header.type = REQUEST) {
					// Extract Body
					Admin adm_temp = admin_from_boby(cmd.Header.Body);
					admin_list.append(adm_temp);
				}
				
			case(CTRL_LIST_DEVICES)
				// Pass device list 
				char[] buff = hardwareTable2String()
				
				cmd_ListDevices= {
					{VERSION, 
					API_CONTROL,
					LIST_DEVICES,
					RESPONSE
					} //
					,
					{buff}
				}
				write
				
			case(CTRL_DEVICE_GONE)
				// Delete device from table
				device_refered = cmd.Body.device_id
				hardware_client_table.delete(device_refered)
			case(STD_OUTDEVICE)
				// Pass packet to Admin 
				write(<Admin>)
	case()
	// Rest of funtions
}




*/