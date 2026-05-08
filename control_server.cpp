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
