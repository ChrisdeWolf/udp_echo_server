/*
** client.c -- a datagram "client" demo
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
#define TIMEOUT_SEC 2
#define MAX_RETRANSMISSIONS 3

Connection connections[MAX_CONNECTIONS];  // TODO: move this into code
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

int copyLineToPacket(FILE *file, Packet *packet, int current_index) {
    char line[1024];  // TODO: use MAXBUFLENGTH
    int i = 0;

    int line_offset = (rand() % 3);

    while (fgets(line, sizeof(line), file) != NULL) {
        // TODO: should only process lines < file_size
        if (i >= current_index && i <= (current_index + line_offset)) {
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
    // TODO: error handle... if(copyLineToPacket())...
    fclose(file);

    // so the server can track what line_index to expect next
    packet->line_end_index = new_offset;

    if (new_offset >= connection->file_size - 1) connection->finished = 1;

    connection->line_index = new_offset + 1;
}

int sendAndWaitForACK(int sockfd, struct addrinfo *p, Packet *packet) {
    int numbytes;
    fd_set read_fds;
    struct timeval timeout;

    if ((numbytes = sendto(sockfd, packet, sizeof(Packet), 0, p->ai_addr,
                           p->ai_addrlen)) == -1) {
        perror("client: sendto");
        return -1;
    }
    printf("client: sent %d bytes to server\n", numbytes);

    // setup timeout timer
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    // use of select here for the async timer
    // https://beej.us/guide/bgnet/html/split/slightly-advanced-techniques.html#select
    int ready = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

    if (ready < 0) {
        perror("Select failed");
        return -1;
    } else if (ready == 0) {
        return 0;
    }

    // check for a returned ACK
    char ack[3];
    socklen_t addr_len = sizeof(p);
    ssize_t ack_length =
        recvfrom(sockfd, ack, sizeof(ack), 0, (struct sockaddr *)&p, &addr_len);

    if (ack_length < 0) {
        perror("ACK receive error");
        return -1;
    }

    return 1;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    srand(time(NULL));  // Initialization, should only be called once

    // TODO: for custom args if i want to add them
    if (argc != 2) {
        fprintf(stderr, "usage: client hostname\n");
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
            perror("client: socket");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to create socket\n");
        return 2;
    }

    // TODO: do i need this...?
    // mark all connections as un-initialized
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        connections[i].initialized = 0;
    }

    // Create an array to hold the packets
    Packet packets[100];

    // Initialize the packets
    for (int i = 0; i < 100; i++) {
        clearPacketBuffer(&packets[i]);
        packets[i].file_size = -1;  // Initialize file_size to an invalid value
    }

    // Create an index for packets
    int packetIndex = 0;

    while (allConnectionsFinished() != 1) {
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

        // Generate the payload for the current connection
        clearPacketBuffer(&packets[packetIndex]);  // Clear the buffer
        generatePayload(connection, &packets[packetIndex]);

        // Move to the next index for the next packet
        packetIndex++;

        printf("\n");
    }

    // Shuffle the packets
    for (int i = packetIndex - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Packet temp = packets[i];
        packets[i] = packets[j];
        packets[j] = temp;
    }

    // Now send the shuffled packets
    for (int i = 0; i < packetIndex; i++) {
        if (packets[i].file_size != -1) {
            int retransmissions = 0;
            while (retransmissions <= MAX_RETRANSMISSIONS) {
                int result = sendAndWaitForACK(sockfd, p, &packets[i]);
                if (result == 0) {
                    printf("timeout! re-transmitting...\n");
                    retransmissions++;
                } else if (result == -1) {
                    // Handle error
                    printf("error!\n");
                    break;
                } else {
                    printf("ACK received!\n");
                    break;
                }
            }
            if (retransmissions > MAX_RETRANSMISSIONS) {
                printf("Max retransmissions reached for packet. Aborting.\n");
                break;
            }
        }
    }

    freeaddrinfo(servinfo);
    close(sockfd);

    return 0;
}
