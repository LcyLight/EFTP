/*
Name: YiTing OuYang
UCID: 30140886
Course: CPSC 441 T03
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>

//define all the max length of the input
#define USERNAME_MAX_LENGTH 32
#define PASSWORD_MAX_LENGTH 32
#define FILENAME_MAX_LENGTH 255
#define SEGMENT_LENGTH 1024
#define ERROR_MSG_LENGTH 512

//initialize all the variables
char username[USERNAME_MAX_LENGTH];
char password[PASSWORD_MAX_LENGTH];
int port;
char directory[FILENAME_MAX_LENGTH];
int session_number;
char filename[FILENAME_MAX_LENGTH];


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

//create a union to store all the message types
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

//create a struct to store the timer
struct timer{
    long seconds;
    long mseconds;
};

int main(int argc, char *argv[]) {

    //add command line arguments (./c <Username> <Password> <Port> <Directory>)
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <Username> <Password> <Port> <Directory>\n", argv[0]);
        exit(1);
    }

    //get all the command line arguments
    char *username = argv[1];
    char *password = argv[2];
    int port = atoi(argv[3]);
    char *directory = argv[4];

    //Authentication phase

    //initialize socket and address
    int sockfd, sockfd2;
    struct sockaddr_in server_addr, client_addr;
    struct sockaddr *server, *client;

    //initialize error count
    int error_count;
    bool error_loop = false;

    //server create UDP socket and bind to user input port
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    //set up server address
    server = (struct sockaddr *) &server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    //bind socket to port
    if (bind(sockfd, server, sizeof(server_addr)) < 0) {
        perror("ERROR on binding");
        close(sockfd);
        exit(1);
    }else{
        printf("\nServer is created! Listening to client...\n\n");
    }

    //set up client address
    client = (struct sockaddr *) &client_addr;
    socklen_t client_len = sizeof(client_addr);

    loop:
    //loop to keep the server running
    while(1){

        //bool variables to control the loop
        bool keep_loop = true;
        bool last_packet = false;
        bool write_block = false;
        int num_bytes = 0;

        //receive authentication message from client
        eftp_message AUTH;
        if (recvfrom(sockfd, &AUTH, sizeof(AUTH), 0, client, &client_len) < 0) {
            perror("ERROR receiving authentication message from client");
            close(sockfd);
            exit(1);
        }else{
            printf("Received message from client\n");
        }

        //check if the opcode is AUTH
        if (AUTH.opcode == 01) {
            printf("Received authentication message from client\n");
        } else {
            printf("ERROR: Received wrong opcode from client\n");
            close(sockfd);
            exit(1);
        }

        //create new socket for data transfer with random port number
        sockfd2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockfd2 < 0) {
            perror("ERROR opening socket");
            close(sockfd);
            close(sockfd2);
            exit(1);
        }

        struct timer timeout;
        timeout.seconds = 5;
        timeout.mseconds = 0;

        // set timeout for socket
        if (setsockopt(sockfd2, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("ERROR setting timeout");
            close(sockfd);
            close(sockfd2);
            exit(1);
        }


        //set up new server address
        server = (struct sockaddr *) &server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        server_addr.sin_port = htons(0);

        //bind socket to port
        if (bind(sockfd2, server, sizeof(server_addr)) < 0) {
            perror("ERROR on binding");
            close(sockfd);
            close(sockfd2);
            exit(1);
        }else{
            printf("\nNew socket created for data transfer\n\n");
        }
        
        //check if the username and password are correct
        if (strcmp(AUTH.auth.username, username) == 0 && strcmp(AUTH.auth.password, password) == 0) {
            printf("Authentication successful!\n\n");

        } else {

            error_loop = true;
            while (error_loop == true){
                error_loop = false;
                //send error message to client
                eftp_message ERROR;
                ERROR.opcode = 06;
                strcpy(ERROR.error.error_string, "Passowrd or username is incorrect");
                if (sendto(sockfd2, &ERROR, sizeof(ERROR), 0, client, client_len) < 0) {
                    error_count++;
                    
                    if (error_count > 3) {
                        perror("ERROR sending error message");
                        goto loop;

                    } else {
                        printf("ERROR sending error message, trying again...\n");
                        error_loop = true;
                        continue;
                    }

                }else{
                    printf("\nAuthentication failed\n");
                    error_count = 0;
                }
            }
                
        }

        //generate random session number from 1 to 65535
        srand(time(NULL));
        session_number = rand() % 65535 + 1;

        error_loop = true;
        while (error_loop == true){
            error_loop = false;   
            //send acknowledgement message to client
            eftp_message ACK;
            ACK.opcode = 05;
            ACK.ack.session_num = session_number;
            ACK.ack.block_num = 0;
            ACK.ack.segment_num = 0;

            //retransmit if the message is not received and timeout if 5 seconds is reached
            if (sendto(sockfd2, &ACK, sizeof(ACK), 0, client, client_len) < 0) {
                error_count++;

                if (error_count > 3) {
                        perror("ERROR sending acknowledgement message");
                        goto loop;

                } else {
                    printf("ERROR sending acknowledgement message, trying again...\n");
                    error_loop = true;
                    continue;
                }

            }else{
                printf("Sent acknowledgement with session number to client\n");
                error_count = 0;
            }
        }

        //reveive request message from client
        eftp_message REQUEST;
        if (recvfrom(sockfd2, &REQUEST, sizeof(REQUEST), 0, client, &client_len) < 0) {
            perror("ERROR on recvfrom");
            goto loop;
        }else{
            printf("\nReceived request message from client\n\n");
        }

        //check if the session number is correct
        if (REQUEST.request.session_num == session_number) {
            printf("Session number is correct\n");
        } else {
            printf("ERROR: Session number is incorrect\n");
            goto loop;
        }

        //check if the opcode is RRQ or WRQ
        if (REQUEST.opcode == 02) {

            printf("Received RRQ message from client\n");

            // download
            strcpy(filename, REQUEST.request.filename);
            unsigned char segment_byte;
            unsigned char segment_array[1025];

            // set up variables for sending segments
            int ind = 0;
            int total = 0;

            // start with block 1 and segment 1
            int block_number = 1;
            int segment_number = 1;

            // set up bool variables to control the loop
            bool not_EOF = true;
            bool segment_send = false;
            bool empty_segment = false;

            // set up the directory location
            char directory_location[32];
            strcpy(directory_location, directory);
            strcat(directory_location, "/");
            strcat(directory_location, filename);

            // open the file
            FILE * file;
            file = fopen(directory_location, "rb");
            //if file does not exist, send error message
            if (file == NULL){
                printf("ERROR: File or directory does not exist\n");
                goto loop;
            }else{
                printf("File opened for reading!\n");
            }

            // get the file size
            long file_size = fsize(file);

            printf("\nThe file size is %ld\n", file_size);
            
            // check if the file size is a multiple of 1024, if true, send an empty segment at the end
            if (file_size % 1024 == 0){
                empty_segment = true;
            }

            // check if the file is opened successfully
            if (file < 0){
                // send error and exit
                goto loop;
            } else {
                printf("File opened for reading!\n");

                // while loop to read through file contents
                while (not_EOF == true){
                    
                    // read the file byte by byte
                    segment_byte = getc(file);

                    // if we reach the end of the file, set the last segment bool to true, and stop while loop
                    if (total == file_size){
                        segment_send = true;
                        not_EOF = false;

                    } else {

                        //copy the byte into the segment array
                        segment_array[ind] = segment_byte;

                        //increment the index and total
                        ind++;
                        total++;

                        // if the index reaches 1024, set the segment send bool to true
                        if (ind == 1024){
                            segment_send = true;
                        }
                    }

                    // if the segment send bool is true, send the segment
                    if (segment_send == true){
                        segment_send = false;

                        error_loop = true;
                        while (error_loop == true){
                            error_loop = false;   
                            //create data message
                            eftp_message DATA;
                            DATA.data.opcode = 04;
                            DATA.data.session_num = 0000;
                            DATA.data.block_num = block_number;
                            DATA.data.segment_num = segment_number;
                            DATA.data.segment_size = ind;

                            //copy the segment array into the data message
                            for (int i = 0; i < ind; i++){
                                DATA.data.segment_data[i] = segment_array[i];
                            }

                            //retransmit if the message is not received and timeout if 5 seconds is reached
                            if (sendto(sockfd2, &DATA, sizeof(DATA), 0, client, client_len) < 0) {
                                error_count++;

                                if (error_count > 3) {
                                    perror("ERROR sending data message");
                                    goto loop;

                                } else {
                                    printf("ERROR sending data message, trying again...\n");
                                    error_loop = true;
                                    continue;
                                }
                            }else{
                                error_count = 0;
                            }
                        }

                        //bzero the segment array
                        bzero(segment_array, 1025);

                        //get the ack message from client
                        eftp_message DATAACK;
                        if (recvfrom(sockfd2, &DATAACK, sizeof(DATAACK), 0, client, &client_len) < 0) {
                            perror("ERROR on recvfrom");
                            goto loop;
                        }

                        //check if its an ack message
                        if (DATAACK.data.opcode != 05){
                            printf("invalid packet received\n");
                            goto loop;
                        }

                        //check if the ack message is correct
                        if (DATAACK.data.block_num != block_number || DATAACK.data.segment_num != segment_number){
                            printf("invalid ack packet received\n");
                            goto loop;
                        }

                        //increment the block and segment numbers
                        segment_number++;
                        if (segment_number == 9){
                            block_number++;
                            segment_number = 1;
                        }

                        //reset the index
                        bzero(segment_array, 1025);
                        ind = 0;

                        // if the file is at the end, close the file
                        if (not_EOF == false){
                            if (empty_segment == true){
                                fclose(file);
                            }    
                        }
                    }
                }
                printf("File transfer complete!\n\n");
            }

            //write request
        } else if(REQUEST.opcode == 03){
            printf("Received WRQ message from client\n");

            error_loop = true;
            while (error_loop == true){
                error_loop = false;   
                //send ack message to client
                eftp_message ACK;
                ACK.opcode = 05;
                ACK.ack.session_num = session_number;
                ACK.ack.block_num = 1;
                ACK.ack.segment_num = 0;

                //retransmit if the message is not received and timeout if 5 seconds is reached
                if (sendto(sockfd2, &ACK, sizeof(ACK), 0, client, client_len) < 0) {
                    error_count++;

                    if (error_count > 3) {
                        perror("ERROR sending ack message");
                        goto loop;

                    } else {
                        printf("ERROR sending ack message, trying again...\n");
                        error_loop = true;
                        continue;
                    }
                }else{
                    printf("Sent acknowledgement of WRQ to client\n");
                    error_count = 0;
                }
            }

            //receive data message from client
            eftp_message DATA; 

            //get the filename
            strcpy(filename, REQUEST.request.filename);

            //set up the directory location
            char directory_location[32];
            strcpy(directory_location, directory);
            strcat(directory_location, "/");
            strcat(directory_location, filename);

            //open the file
            FILE * file;
            file = fopen(directory_location, "wb");
            //if file doest exist, print error and exit
            if (file == NULL){
                printf("Error writing to file!\n");
                goto loop;
            } else {
                printf("File opened for writing!\n");
            }

            printf("Uploading file to server!\n");

            //set up variables for receiving segments
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

                //receive data message from client
                if (recvfrom(sockfd2, &DATA, sizeof(DATA), 0, (struct sockaddr *) client, &client_len) < 0) {
                    perror("ERROR receiving response from client");
                    goto loop;
                }

                //check if its a data message
                if (DATA.data.opcode != 04){
                    printf("invalid packet received\n");
                    goto loop;;
                }

                if (DATA.data.segment_size == 0){
                    last_packet = true;
                }

                if (DATA.data.segment_size < 1024){
                    last_packet = true;
                    write_block = true;
                }

                //copy the segment into the correct array
                if (DATA.data.segment_num == 1){
                    memcpy(buf1, DATA.data.segment_data, DATA.data.segment_size);
                    num_bytes += DATA.data.segment_size;

                } else if (DATA.data.segment_num == 2) {
                    memcpy(buf2, DATA.data.segment_data, DATA.data.segment_size);
                    num_bytes += DATA.data.segment_size;

                } else if (DATA.data.segment_num == 3) {
                    memcpy(buf3, DATA.data.segment_data, DATA.data.segment_size);
                    num_bytes += DATA.data.segment_size;
        
                } else if (DATA.data.segment_num == 4) {
                    memcpy(buf4, DATA.data.segment_data, DATA.data.segment_size);
                    num_bytes += DATA.data.segment_size;

                } else if (DATA.data.segment_num == 5) {
                    memcpy(buf5, DATA.data.segment_data, DATA.data.segment_size);
                    num_bytes += DATA.data.segment_size;

                } else if (DATA.data.segment_num == 6) {
                    memcpy(buf6, DATA.data.segment_data, DATA.data.segment_size);
                    num_bytes += DATA.data.segment_size;

                } else if (DATA.data.segment_num == 7) {
                    memcpy(buf7, DATA.data.segment_data, DATA.data.segment_size);
                    num_bytes += DATA.data.segment_size;

                } else if (DATA.data.segment_num == 8) {
                    memcpy(buf8, DATA.data.segment_data, DATA.data.segment_size);
                    num_bytes += DATA.data.segment_size;

                    write_block = true;
                }

                //write the block to the file
                if (write_block == true){
                    write_block = false;

                    //write each segment in order
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

                    //write block to file
                    fwrite(buf, num_bytes, 1, file);

                    //clear the block
                    bzero(buf, 8193);

                    num_bytes = 0;
                }

                error_loop = true;
                while (error_loop == true){
                    error_loop = false;   
                    //send ack message to client with opcode 04
                    eftp_message ack;
                    ack.opcode = 05;
                    ack.ack.session_num = session_number;
                    ack.ack.block_num = DATA.data.block_num;
                    ack.ack.segment_num = DATA.data.segment_num;

                    //retransmit if the message is not received and timeout if 5 seconds is reached
                    if (sendto(sockfd2, &ack, sizeof(ack), 0, (struct sockaddr *) client, client_len) < 0) {
                        error_count++;
                        
                        if (error_count > 3) {
                            perror("ERROR sending ack message");
                            goto loop;

                        } else {
                            printf("ERROR sending ack message, trying again...\n");
                            error_loop = true;
                            continue;
                        }
                    }else{
                        error_count = 0;
                    }
                }

                //check if its the last packet
                if (last_packet){
                    fclose(file);
                    printf("File uploaded to server successfully!\n\n");
                }
                
                //if its last packet, break
                if (last_packet){
                    printf("returning to listen\n");
                    break;
                }
            }

        } else {
            printf("ERROR: Received wrong opcode from client\n");
            goto loop;
        }

    }

    //close the sockets and exit
    close(sockfd);
    close(sockfd2);
    exit(0);

}

