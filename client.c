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

#define UNKNOWN_FILE_LENGTH -1
#define MAX_PACKET_SIZE 1024
#define pLoss   0
#define pCorrupt 0

void error(char *msg)
{
    perror(msg);
    exit(0);
}

double randomNum()
{
    //returns random number from [0...1]
    double num = (double)rand() / (double)RAND_MAX ;
    //printf("prob is %f", num);
    return num;
}
int parseChunk(char* newBuffer, int& seqNum, int&fileSize, char* contents) {
    //stores sequence number, file size, and file contents
    //returns size of file
    
    //TO DO:
    //strtok was causing problems, so brute force for now
    //maybe optimize if time later
    
    int i = 0;
    int start, end;
    
    //obtain seq num
    while (1) {
        char cur = *(newBuffer + i);
        if (cur == ':') {
            start = i+2;
        }
        else if (cur == '\n') {
            end = i-1;
            break;
        }
        i++;
    }
    int seqLength = end - start + 1;
    char* seqNumString = (char*)malloc(seqLength+1);
    seqNumString[seqLength] = '\0';
    memcpy(seqNumString, newBuffer + start, seqLength);
    seqNum = atoi(seqNumString);
    //printf("seq num is %d\n", seqNum);
    
    //obtain file size
    while (1) {
        char cur = *(newBuffer + i);
        if (cur == ':') {
            start = i+2;
        }
        else if (cur == 'B') {
            end = i-1;
            break;
        }
        i++;
    }
    
    int length = end - start + 1;
    char* fileSizeString = (char*)malloc(length+1);
    fileSizeString[length] = '\0';
    memcpy(fileSizeString, newBuffer + start, length);
    fileSize = atoi(fileSizeString);
    //printf("f size is %d\n", fileSize);
    
    start = i+3;
    while (1) {
        char cur = *(newBuffer + i);
        if (cur == '\0') {
            end = i;
            break;
        }
        i++;
    }
    
    int contentLength = end - start; // account for excluding null byte
    memcpy(contents, newBuffer + start, contentLength);
    //contents[contentLength] = '\0';
    //printf("contents\n%s\n", contents);
    
    return contentLength;
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
    
    char buffer[MAX_PACKET_SIZE];
    socklen_t slen = sizeof(struct sockaddr_in);
    int count = 0;
    int totalLength = 0;
    int fileSize = UNKNOWN_FILE_LENGTH;
    FILE *file = fopen(strcat(filename, "_1"), "wb"); //to do, fix filename
    
    while (fileSize == UNKNOWN_FILE_LENGTH || totalLength < fileSize) {
        memset(buffer, 0, sizeof(buffer));
        int isLost = (randomNum() < pLoss);
        int isCorrupt = (randomNum() < pCorrupt);
        int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *) &serv_addr, &slen);
        
        if (n == -1 || isLost) {
            printf("packet lost\n");
            //printf("an error: %s\n", strerror(errno));
        }
        else if (isCorrupt) {
            printf("packet is corrupt\n");
        }
        else if (n == 0) {
            printf("empty\n");
        }
        else {
            printf("Received %d bytes\n", n);
            //printf("buffer is %s\n", buffer);
            
            //TO DO: store file
        
            
            //copy buffer contents
            char* newBuffer = (char*)malloc(MAX_PACKET_SIZE);
            memcpy(newBuffer, buffer, MAX_PACKET_SIZE);
            
        
            int seqNum;
            char* contents;
            contents = (char*)malloc(MAX_PACKET_SIZE);
            memset(contents, 0, MAX_PACKET_SIZE);
            int contentLength = parseChunk(newBuffer, seqNum, fileSize, contents);
            totalLength+= contentLength;
            fwrite(contents, sizeof(char), contentLength, file);
            printf("seqNum is %d\n", seqNum);
            printf("file size is %d\n", fileSize);
            printf("Contents are\n%s", contents);
            
            //send ack
            char* ack = (char*)malloc(32);
            sprintf(ack, "ACK: %d", seqNum);
            if (sendto(sockfd, ack, strlen(ack), 0, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
                printf("error\n");
            }
            else {
                //printf("ack success\n");
                printf("sent %s\n", ack);
                count++;
            }

        }
    }

    fclose(file);
    close(sockfd); //close socket
        
        return 0;
    

}
