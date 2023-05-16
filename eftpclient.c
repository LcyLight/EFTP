/*
Name: YiTing OuYang
UCID: 30140886
Course: CPSC 441 T03
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>

#define USERNAME_MAX_LENGTH 32
#define PASSWORD_MAX_LENGTH 32
#define FILENAME_MAX_LENGTH 255
#define SEGMENT_LENGTH 1024
#define ERROR_MSG_LENGTH 512

char username[32];
char password[32];
int ip;
int port;
char UorD;
int session_number;

//Get file size
long fsize(FILE *file){
    //get current position
    int previous = ftell(file);
    fseek(file, 0L, SEEK_END);
    int size = ftell(file);
    //go back to where we were
    fseek(file,previous,SEEK_SET); 
    return size;
}

//initialize a union for all the messages
typedef union{

    int opcode;

    struct {
        int opcode; // AUTH
        char username[USERNAME_MAX_LENGTH + 1];
        char password[PASSWORD_MAX_LENGTH + 1];
    } auth;

    struct {
        int opcode; // RRQ OR WRQ  
        int session_num;
        char filename[FILENAME_MAX_LENGTH + 1];
    } request;

    struct {
        int opcode; // DATA
        int session_num;
        int block_num;
        int segment_num;
        int segment_size;
        unsigned char segment_data[SEGMENT_LENGTH];
    } data;

    struct {
        int opcode; // ACK 
        int session_num;
        int block_num;
        int segment_num;
    } ack;

    struct {
        int opcode; // ERROR
        char error_string[ERROR_MSG_LENGTH + 1];
    } error;

} eftp_message;

//initialize a struct for timer
struct timer{
    long seconds;
    long mseconds;
};

int main(int argc, char *argv[]) {

    //initialize variables
    bool keep_loop = true;
    bool last_packet = false;
    bool write_block = false;
    int num_bytes = 0;

    bool error_loop;
    int error_count = 0;

    //add command line arguments (./client [username] [password] [ip] [port] [upload/download] [filename])
    if (argc != 7){
        fprintf(stderr, "Usage: %s <Username> <Password> <ip> <Port> <U/D> <filename>\n", argv[0]);
        exit(1);
    }else{
        printf("command line arguments recorded: %s %s %s %s %s %s\n\n", argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
    }

    //assign command line arguments to variables
    char *username = argv[1];
    char *password = argv[2];
    char *ip = argv[3];
    int port = atoi(argv[4]);
    char *UorD = argv[5];
    char *filename = argv[6];

    //Authentication phase
    int sockfd;
    struct sockaddr_in server_addr, client_addr, new_server_addr;
    struct sockaddr *server, *client, *new_server;

    //create client socket
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    //set timeout for socket
    struct timer timeout;
    timeout.seconds = 5;
    timeout.mseconds = 0;

    // set timeout for socket
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("ERROR setting timeout");
        exit(1);
    }

    //set up client address
    client = (struct sockaddr *) &client_addr;
    socklen_t client_len = sizeof(client_addr);
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    client_addr.sin_port = htons(0);

    //bind client socket to client address
    if (bind(sockfd, (struct sockaddr *) client, client_len) < 0) {
        perror("ERROR binding client socket");
        exit(1);
    }else{
        printf("socket to server authentication created\n");
    }

    //set up server address
    server = (struct sockaddr *) &server_addr;
    socklen_t server_len = sizeof(server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);

    //while loop for retransmission
    error_loop = true;
    while (error_loop == true){
        error_loop = false;

        //send authentication message to server with opcode 01
        eftp_message AUTH;
        AUTH.opcode = 01;
        //send username and password
        strcpy(AUTH.auth.username, username);
        strcpy(AUTH.auth.password, password);

        //send authentication message to server
        if (sendto(sockfd, &AUTH, sizeof(AUTH), 0, (struct sockaddr *) server, server_len) < 0) {
            error_count++;

            if (error_count > 3) {
                perror("ERROR sending authentication message");
                close(sockfd);
                exit(1);

            } else {
                printf("ERROR sending authentication message, trying again...\n");
                error_loop = true;
                continue;
            }
        }else{
            printf("authentication message sent to server\n");
            error_count = 0;
        }

    }

    //receive response from server
    eftp_message response;
    if (recvfrom(sockfd, &response, sizeof(response), 0, (struct sockaddr *) server, &server_len) < 0) {
        perror("ERROR receiving response from server");
        close(sockfd);
        exit(1);
    }else{
        printf("response received from server\n");
    }

    //if opcode is 06, authentication was unsuccessful
    if (response.opcode == 06) {
        //print error message
        printf("ERROR: %s\n", response.error.error_string);
        close(sockfd);
        exit(1);
    }else{
        printf("\nAuthentication successful!\n");
    }

    //get session number from ack message
    session_number = response.ack.session_num;

    printf("Session number: %d\n\n", session_number);

    //if user entered 'D' or 'd' for UorD input, download file
    if(UorD[0] == 'D' || UorD[0] == 'd'){

        FILE * file;
        file = fopen(filename, "wb");
        //if file does not exist, print error message
        if (file == NULL) {
            perror("ERROR opening file for writing!");
            close(sockfd);
            exit(1);
        }else{
            printf("file opened\n");
        }

        error_loop = true;
        while (error_loop == true){
            error_loop = false;     

            //send RRQ message to server with opcode 02
            eftp_message RRQ;
            RRQ.opcode = 02;
            //send session number and filename
            RRQ.request.session_num = session_number;
            strcpy(RRQ.request.filename, filename);

            error_loop = true;
            while (error_loop == true){
                error_loop = false;   
        
                //send RRQ message to server
                if (sendto(sockfd, &RRQ, sizeof(RRQ), 0, (struct sockaddr *) server, server_len) < 0) {
                    error_count++;
                    if (error_count > 3) {
                        perror("ERROR sending RRQ message");
                        close(sockfd);
                        exit(1);

                    } else {
                        printf("ERROR sending RRQ message, trying again...\n");
                        error_loop = true;
                        continue;
                    }
                }else{
                    printf("RRQ message sent to server\n\n");
                    error_count = 0;

                }
            }
        }

        printf("Downloading file!\n");

        //set char arrays to 0
        unsigned char buf1[1025];
        unsigned char buf2[1025];
        unsigned char buf3[1025];
        unsigned char buf4[1025];
        unsigned char buf5[1025];
        unsigned char buf6[1025];
        unsigned char buf7[1025];
        unsigned char buf8[1025];
        unsigned char buf[8193];

        while (keep_loop == true){

            //receive data from server
            if (recvfrom(sockfd, &response, sizeof(response), 0, (struct sockaddr *) server, &server_len) < 0) {
                perror("ERROR receiving response from server");
                close(sockfd);
                exit(1);
            }

            //if packet is not a data packet, print error message
            if (response.data.opcode != 04){
                printf("invalid packet received\n");
                close(sockfd);
                exit(1);
            }

            //if segment is empty, set last packet to true
            if (response.data.segment_size == 0){
                last_packet = true;
            }

            //if segment is less than 1024 bytes, set last packet to true
            if (response.data.segment_size < 1024){
                last_packet = true;
                write_block = true;
            }

            //copy each segment into a buffer according to segment number
            if (response.data.segment_num == 1){
                memcpy(buf1, response.data.segment_data, response.data.segment_size);
                num_bytes += response.data.segment_size;

            } else if (response.data.segment_num == 2) {
                memcpy(buf2, response.data.segment_data, response.data.segment_size);
                num_bytes += response.data.segment_size;

            } else if (response.data.segment_num == 3) {
                memcpy(buf3, response.data.segment_data, response.data.segment_size);
                num_bytes += response.data.segment_size;
    
            } else if (response.data.segment_num == 4) {
                memcpy(buf4, response.data.segment_data, response.data.segment_size);
                num_bytes += response.data.segment_size;

            } else if (response.data.segment_num == 5) {
                memcpy(buf5, response.data.segment_data, response.data.segment_size);
                num_bytes += response.data.segment_size;

            } else if (response.data.segment_num == 6) {
                memcpy(buf6, response.data.segment_data, response.data.segment_size);
                num_bytes += response.data.segment_size;

            } else if (response.data.segment_num == 7) {
                memcpy(buf7, response.data.segment_data, response.data.segment_size);
                num_bytes += response.data.segment_size;

            } else if (response.data.segment_num == 8) {
                memcpy(buf8, response.data.segment_data, response.data.segment_size);
                num_bytes += response.data.segment_size;

                //if all 8 buffers are full, write to file
                write_block = true;
            }

            //if ready to write to file
            if (write_block == true){
                write_block = false;

                //merge all 8 buffers into one buffer
                for (int i = 0; i < 1024; i++){

                    buf[i] = buf1[i];
                    buf[i+1024] = buf2[i];
                    buf[i+2048] = buf3[i];
                    buf[i+3072] = buf4[i];
                    buf[i+4096] = buf5[i];
                    buf[i+5120] = buf6[i];
                    buf[i+6144] = buf7[i];
                    buf[i+7168] = buf8[i];

                }

                //write to file
                fwrite(buf, num_bytes, 1, file);

                //reset buffers
                bzero(buf, 8193);

                num_bytes = 0;
            }


            error_loop = true;
            while (error_loop == true){
                error_loop = false;

                //send ack message to server with opcode 04
                eftp_message ack;
                ack.opcode = 05;
                ack.ack.session_num = session_number;
                ack.ack.block_num = response.data.block_num;
                ack.ack.segment_num = response.data.segment_num;

                if (sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *) server, server_len) < 0) {
                    error_count++;
                    if (error_count > 3) {
                        perror("ERROR sending ack message");
                        close(sockfd);
                        exit(1);

                    } else {
                        printf("ERROR sending ack message, trying again...\n");
                        error_loop = true;
                        continue;
                    }
                }else{
                    error_count = 0;
                }
            }

            //if last packet, close file and exit
            if (last_packet){
                fclose(file);
                close(sockfd);
                printf("File downloaded successfully!\n\n");
                exit(0);
            }
            
        }

        
    //if user entered 'U' or 'u' for UorD input, upload file
    }else if(UorD[0] == 'U' || UorD[0] == 'u'){ 

        error_loop = true;
        while (error_loop == true){
            error_loop = false;
            //send WRQ message to server with opcode 03
            eftp_message WRQ;
            WRQ.opcode = 03;
            //send session number and filename
            WRQ.request.session_num = session_number;
            strcpy(WRQ.request.filename, filename);

            //send WRQ message to server
            if (sendto(sockfd, &WRQ, sizeof(WRQ), 0, (struct sockaddr *) server, server_len) < 0) {
                error_count++;
                if (error_count > 3) {
                    perror("ERROR sending WRQ message");
                    close(sockfd);
                    exit(1);

                } else {
                    printf("ERROR sending WRQ message, trying again...\n");
                    error_loop = true;
                    continue;
                }
            }else{
                printf("WRQ message sent to server\n");
                error_count = 0;
            }
        }


        //receive ack message from server
        eftp_message ack;
        if (recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *) server, &server_len) < 0) {
            perror("ERROR receiving ack message from server");
            close(sockfd);
            exit(1);
        }

        //if message is ack message, print "starting file upload"
        if(ack.opcode == 05){
            printf("Starting file upload\n");
        }else{
            printf("Wrong message type received\n");
            close(sockfd);
            exit(1);
        }

        //initialize variables
        unsigned char segment_byte;
        unsigned char segment_array[1025];

        int ind = 0;
        int total = 0;

        int block_number = 1;
        int segment_number = 1;

        bool not_EOF = true;
        bool segment_send = false;
        bool empty_segment = false;

        //open file for reading
        FILE * file;
        file = fopen(filename, "rb");
        //if file does not exist, send error message and exit
        if (file == NULL){
            // send error and exit
            printf("File does not exist!\n");
            close(sockfd);
            exit(1);
        }else{
            printf("File opened for reading!\n");
        }

        //get file size
        long file_size = fsize(file);

        printf("the file size is %ld\n", file_size);
        
        //if file size is a multiple of 1024, set empty_segment to true
        if (file_size % 1024 == 0){
            empty_segment = true;
        }

        if (file < 0){
            // send error and exit
            close(sockfd);
            exit(1);
        } else {
            // while loop to read through file contents
            printf("File opened for reading!\n");

            //while not end of file
            while (not_EOF == true){
                
                //get each byte of file
                segment_byte = getc(file);

                //if segment is full, send segment
                if (total == file_size){
                    segment_send = true;
                    not_EOF = false;

                } else {
                    //add byte to segment array
                    segment_array[ind] = segment_byte;
                    ind++;
                    total++;

                    //if segment is full, send segment
                    if (ind == 1024){
                        segment_send = true;
                    }
                }

                //if segment is full, send segment
                if (segment_send == true){
                    segment_send = false;

                    error_loop = true;
                    while (error_loop == true){
                        error_loop = false;

                        //send data message to server with opcode 04
                        eftp_message DATA;

                        DATA.data.opcode = 04;
                        DATA.data.session_num = ack.ack.session_num;
                        DATA.data.block_num = block_number;
                        DATA.data.segment_num = segment_number;
                        DATA.data.segment_size = ind;

                        //copy segment array to segment data
                        for (int i = 0; i < ind; i++){
                            DATA.data.segment_data[i] = segment_array[i];
                        }

                        if (sendto(sockfd, &DATA, sizeof(DATA), 0, server, server_len) < 0) {
                            error_count++;
                            if (error_count > 3) {
                                perror("ERROR sending data message");
                                close(sockfd);
                                exit(1);

                            } else {
                                printf("ERROR sending data message, trying again...\n");
                                error_loop = true;
                                continue;
                            }
                        }else{
                            error_count = 0;
                        }
                    }

                    //reset segment array
                    bzero(segment_array, 1025);

                    //receive ack message from server
                    eftp_message DATAACK;
                    if (recvfrom(sockfd, &DATAACK, sizeof(DATAACK), 0, server, &server_len) < 0) {
                        perror("ERROR on recvfrom");
                        close(sockfd);
                        exit(1);
                    }

                    //check if its ack message
                    if (DATAACK.data.opcode != 05){
                        printf("invalid packet received\n");
                        close(sockfd);
                        exit(1);
                    }
        
                    //check if ack message is for correct block and segment
                    if (DATAACK.data.block_num != block_number || DATAACK.data.segment_num != segment_number){
                        printf("invalid ack packet received\n");
                        close(sockfd);
                        exit(1);
                    }

                    //increment segment number and block number
                    segment_number++;
                    if (segment_number == 9){
                        block_number++;
                        segment_number = 1;
                    }
                
                    //reset segment array
                    bzero(segment_array, 1025);
                    ind = 0;

                    //if end of file close file and exit
                    if (not_EOF == false){

                        if (empty_segment == true){
                            fclose(file);
                            close(sockfd);
                            exit(0);
                        }    
                    }
                }
            }
            printf("File transfer complete!\n\n");
        }

    }


}

