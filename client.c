/*
** talker.c -- a datagram "client" demo
*/

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "connection_structs.h"

#define SERVERPORT "7777"  // the port users will be connecting to
#define MAX_CONNECTIONS 10

Connection connections[MAX_CONNECTIONS];
// TODO: turn this into bytes, throughout the whole thing
const int file_sizes[10] = {9, 9, 10, 9, 8, 12, 12, 8, 7, 11};
char file_names[100][256];

int allConnectionsFinished() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i].finished != 1) {
            return 0;
        }
    }
    return 1;
}

int getRandomFileIndex() {
    return rand() % 10;  // Generates a random number between 0 and 9
}

Connection *getConnection(int file_index) {
    if (file_index >= 0 && file_index < MAX_CONNECTIONS) {
        return &connections[file_index];
    } else {
        return NULL;
    }
}

void initConnection(int file_index) {
    Connection *conn = &connections[file_index];
    conn->finished = 0;
    conn->file_size = file_sizes[file_index];
    conn->file_index = file_index;
    conn->line_index = 0;
    conn->initialized = 1;
}

void clearPacketBuffer(Packet *packet) {
    memset(packet->buffer, 0, sizeof(packet->buffer));
}

/*
// int readRandFile(Connection *connection) {
//     DIR *dir;
//     struct dirent *entry;
//     char directory_path[] =
//         "./client_files";  // Replace with your directory path

//     // Open the directory
//     dir = opendir(directory_path);

//     if (dir == NULL) {
//         perror("Unable to open directory");
//         return 1;
//     }

//     // Create an array to store file names
//     int file_count = 0;

//     // Read file names from the directory
//     while ((entry = readdir(dir)) != NULL) {
//         if (entry->d_type == DT_REG) {  // Check if it's a regular file
//             strncpy(file_names[file_count], entry->d_name, 255);
//             file_count++;
//         }
//     }

//     // Close the directory
//     closedir(dir);

//     if (file_count == 0) {
//         printf("No files found in the directory.\n");
//         return 0;
//     }

//     // Generate the random indexes for file and line #
//     int rand_file_index = rand() % file_count;

//     printf("rand_file_index: %i\n", rand_file_index);

//     // Open and read the randomly selected file
//     char file_path[256];
//     snprintf(file_path, sizeof(file_path), "%s/%s", directory_path,
//              file_names[rand_file_index]);
//     FILE *file = fopen(file_path, "r");

//     if (file == NULL) {
//         perror("Unable to open file");
//         return 1;
//     }

//     // fseek(file, 0L, SEEK_END);
//     // // calculating the size of the file
//     // long int file_size = ftell(file);
//     // printf("file_size: %ld\n", file_size);

//     // int *randomValue = getRandomInt();
//     // // Read the file line by line
//     // char line[1000];
//     // while (fgets(line, sizeof(line), file) != NULL)
//     // {
//     // 	printf("%s", line);
//     // }
//     // Close the file

//     fclose(file);

//     connection->file_size = file_sizes[rand_file_index];
//     connection->file_index = rand_file_index;
//     // payload[0] = (char *)rand_file_index;
//     // memset(&rand_file_data, rand_file_index, 1);
//     return 0;
// }
*/

int copyLineToPacket(FILE *file, Packet *packet, int current_index) {
    char line[1024];  // TODO: use MAXBUFLENGTH
    int i = 0;

    int line_offset = (rand() % 3);
    printf("line_offset: %d, current_index: %d\n", line_offset, current_index);

    while (fgets(line, sizeof(line), file) != NULL) {
        printf("i: %d, current_index+line_offset: %d\n", i,
               current_index + line_offset);
        if (i >= current_index && i <= (current_index + line_offset)) {
            printf("line: %s\n", line);
            // Ensure the line can fit within packet->buffer
            if (strlen(line) < sizeof(packet->buffer)) {
                strcat(packet->buffer, line);
            } else {
                printf("Line too long for buffer.\n");
                return 0;  // Line too long for the buffer
            }
        }
        i++;
    }
    return current_index + line_offset;
}

void generatePayload(Connection *connection, Packet *packet) {
    packet->file_size = connection->file_size;
    packet->file_index = connection->file_index;
    packet->line_index = connection->line_index;

    char file_path[256];
    snprintf(file_path, sizeof(file_path), "./client_files/quote%d.txt",
             packet->file_index);
    FILE *file = fopen(file_path, "r");

    int new_offset = copyLineToPacket(file, packet, connection->line_index);
    printf("packet contains %s\n", packet->buffer);
    // TODO: error handle... if(copyLineToPacket())...

    fclose(file);

    if (new_offset >= connection->file_size - 1) connection->finished = 1;

    connection->line_index = new_offset + 1;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    srand(time(NULL));  // Initialization, should only be called once

    // TODO: for custom args if i want to add them
    if (argc != 2) {
        fprintf(stderr, "usage: talker hostname\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;  // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and make a socket
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
            -1) {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n");
        return 2;
    }

    // add my logic here

    // TODO: do i need this...?
    // mark all connections as un-initialized
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        connections[i].initialized = 0;
    }

    while (allConnectionsFinished() != 1) {
        Packet *packet = malloc(sizeof(Packet));
        int randomFileIndex = getRandomFileIndex();
        Connection *connection = getConnection(randomFileIndex);
        if (connection->initialized == 0) {
            printf("Creating a new connection for file_index %d\n",
                   randomFileIndex);
            initConnection(randomFileIndex);
            connection = &connections[randomFileIndex];
        } else {
            printf("Using an existing connection for file_index %d\n",
                   randomFileIndex);
        }

        printf(
            "connection->file_index: %d, "
            "connection->line_index: %d, connection->file_size: %d, "
            "connection->initialized: %d, connection->finished: %d\n",
            connection->file_index, connection->line_index,
            connection->file_size, connection->initialized,
            connection->finished);
        generatePayload(connection, packet);

        printf("Sent Packet: buffer = %s\n", packet->buffer);
        if ((numbytes = sendto(sockfd, packet, sizeof(Packet), 0, p->ai_addr,
                               p->ai_addrlen)) == -1) {
            perror("talker: sendto");
            free(packet);  // Free allocated memory
            exit(1);
        }
        printf("talker: sent %d bytes to %s\n", numbytes, argv[1]);
        clearPacketBuffer(packet);  // Clear the buffer
        free(packet);               // Free allocated memory
    }
    freeaddrinfo(servinfo);
    close(sockfd);

    return 0;
}
