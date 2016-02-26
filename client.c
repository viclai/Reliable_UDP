//PROJECT 2

#include <stdio.h>      // fprintf
#include <stdlib.h>     // atoi
#include <unistd.h>     // getopt
#include <netinet/in.h> // sockaddr_in, htons, INADDR_ANY
#include <sys/socket.h> // socket, bind, sockaddr

#include <sys/types.h>
#include <netdb.h>      // define structures like hostent
#include <strings.h>
#include <string.h>
#include <errno.h>

void error(char *msg)
{
    perror(msg);
    exit(0);
}


int main(int argc, char* argv[])
{
  // TODO: Implement ./client localhost postNumber fileName
    
    // error handling
    if (argc != 4) {
        fprintf(stderr, "must provide 4 arguments");
        exit(0);
    }
    
    //obtain args
    char *hostname = argv[1];
    int portno = atoi(argv[2]);
    char *filename =  argv[3];
    
    //create socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0); //create a new socket
    if (sockfd < 0)
        error("ERROR opening socket");
    
     struct hostent *server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    
    struct sockaddr_in serv_addr;
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; //initialize server's address
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);
    
    char* fileRequestMsg = (char*)malloc(30*sizeof(char));
    sprintf(fileRequestMsg, "File: %s", filename);
    printf("message says %s", fileRequestMsg);
    if (sendto(sockfd, fileRequestMsg, strlen(fileRequestMsg), 0, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR sending request");
    
    printf("Sent request for file w/o local error %s\n", filename);
    
    char buffer[256];
    socklen_t slen = sizeof(struct sockaddr_in);
    
        int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *) &serv_addr, &slen);
        if (n == -1) {
            printf("an error: %s\n", strerror(errno));
        }
        else if (n == 0) {
            printf("empty\n");
        }
        else {
            printf("Received %d bytes\n", n);
            printf("buffer is %s\n", buffer);
            
            //TO DO: store file
            //TO DO: where are rest of bytes??

            //send ack
            char* ack = (char*)malloc(32);
            sprintf(ack, "ACK: 0");
            if (sendto(sockfd, ack, strlen(ack), 0, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
                printf("error\n");
            }
            else {
                printf("ack success\n");
                printf("sent %s", ack);
            }

        }

    

    
    
        
    
    
        close(sockfd); //close socket
        
        return 0;
    

}
