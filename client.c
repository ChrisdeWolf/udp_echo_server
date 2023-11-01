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
#define TIMEOUT_SEC 1
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

int getRandomFileIndex(Connection connections[]) {
    int unfinishedFiles[MAX_CONNECTIONS];
    int unfinishedCount = 0;

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i].finished != 1) {
            unfinishedFiles[unfinishedCount] = i;
            unfinishedCount++;
        }
    }

    if (unfinishedCount == 0) {
        return -1;
    }

    int randomIndex = rand() % unfinishedCount;
    return unfinishedFiles[randomIndex];
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
    char line[MAXBUFLEN];
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
    packet->checksum = getChecksum(packet->buffer);

    if (packet->line_end_index >= connection->file_size - 1)
        connection->finished = 1;

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
    printf(
        "packet->file_size: %d, packet->file_index: %d, packet->line_index: "
        "%d, line_end_index: %d\n",
        packet->file_size, packet->file_index, packet->line_index,
        packet->line_end_index);
    printf("client sent %d bytes to server\n", numbytes);

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

    // Check for a returned ACK or NACK
    char ack[3];
    socklen_t addr_len = sizeof(p);
    ssize_t ack_length =
        recvfrom(sockfd, ack, sizeof(ack), 0, (struct sockaddr *)&p, &addr_len);

    if (ack_length < 0) {
        perror("ACK receive error");
        return -1;
    }

    ack[ack_length] = '\0';  // null-terminate the buffer
    if (strcmp(ack, "ACK") == 0) {
        return 1;
    } else if (strcmp(ack, "NCK") == 0) {
        return 0;
    } else {
        printf("Received an unknown response: %s\n", ack);
        return -1;
    }
}

void receiveWithTimeout(int sockfd, struct addrinfo *p) {
    fd_set read_fds;
    struct timeval timeout;

    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    // use of select here for the async timer
    // https://beej.us/guide/bgnet/html/split/slightly-advanced-techniques.html#select
    int ready = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

    if (ready < 0) {
        perror("Select failed");
        // return -1;
    } else if (ready == 0) {
        printf("timeout! need to just cancel the client if this happens\n");
        // return 0;
    } else {
    }

    // Check for a returned ACK or NACK
    Packet receivedPacket;
    socklen_t addr_len = sizeof(p);
    ssize_t resp_len = recvfrom(sockfd, &receivedPacket, sizeof(Packet), 0,
                                (struct sockaddr *)&p, &addr_len);

    if (resp_len < 0) {
        perror("ACK receive error");
        // return -1;
    }

    printf("Received a packet from the server.\n");
    printf("Packet Buffer: %s\n", receivedPacket.buffer);
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

    while (allConnectionsFinished() != 1) {
        Packet *packet = malloc(sizeof(Packet));
        int randomFileIndex = getRandomFileIndex(connections);
        if (randomFileIndex == -1) break;

        Connection *connection = getConnection(randomFileIndex);
        // TODO: should skip this loop if the file has already completed
        //		... so check for connection->finished == 1
        if (connection->initialized == 0) {
            printf("Creating a new connection for file_index %d\n",
                   randomFileIndex);
            initConnection(randomFileIndex);
            connection = &connections[randomFileIndex];
        } else {
            printf("Using an existing connection for file_index %d\n",
                   randomFileIndex);
        }

        generatePayload(connection, packet);

        int retransmissions = 0;

        while (retransmissions <= MAX_RETRANSMISSIONS) {
            int result = sendAndWaitForACK(sockfd, p, packet);
            if (result == 0) {
                printf("timeout or NACK! re-transmitting...\n");
                retransmissions++;
            } else if (result == -1) {
                // TODO: handle?
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

        clearPacketBuffer(packet);  // Clear the buffer
        free(packet);               // Free allocated memory
        printf("\n");
    }

    // CHATGPT: function call here
    receiveWithTimeout(sockfd, p);

    freeaddrinfo(servinfo);
    close(sockfd);

    return 0;
}
