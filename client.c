/*
 *   Author: Christopher deWolf
 *   client.c -- a datagram "client"
 *      Randomly sends data for 10 files to a specified IP address.
 *      Handles retransmissions and waits to receive server response
 *      after all files have been transmitted.
 *      Adapted from "Beej's Guide to Network Programming" (C) 2017
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

#include "client_socket_utils.h"
#include "connection_structs.h"
#include "socket_utils.h"

Connection connections[MAX_FILES];
const int file_sizes[10] = {9, 9, 10, 9, 8, 12, 12, 8, 7, 11};
char file_names[100][256];

/* cleanupClientFiles - cleanup concatendated.txt file */
void cleanupClientFiles() { remove("./client_files/concatenated.txt"); }

/*
 * getServiceFromRegistry - returns the registered service from registry_server
 */
BroadcastPacket getServiceFromRegistry() {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int attempts = 0;
    fd_set read_fds;
    struct timeval timeout;

    /* socket configuration */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;

    // setup request packet
    BroadcastPacket requestPacket;
    strncpy(requestPacket.service_ip, REGISTRY_SERVER_IP, INET_ADDRSTRLEN);
    requestPacket.service_port = atoi(REGISTRY_SERVER_PORT);
    requestPacket.receive_service = 1;  // to get the registry service info
    requestPacket.register_service = 0;

    if ((rv = getaddrinfo(REGISTRY_SERVER_IP, REGISTRY_SERVER_PORT, &hints,
                          &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        requestPacket.service_port = -1;  // indicates an error
        return requestPacket;
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
        requestPacket.service_port = -1;
        freeaddrinfo(servinfo);
        return requestPacket;
    }

    while (attempts < MAX_RETRANSMISSIONS) {
        if (sendto(sockfd, &requestPacket, sizeof(BroadcastPacket), 0,
                   p->ai_addr, p->ai_addrlen) == -1) {
            perror("sendto");
            requestPacket.service_port = -1;
            return requestPacket;
        } else {
            printf(
                "Requesting service information from the registry server...\n");
        }

        // setup timeout timer, longer timeout to give registry server some time
        timeout.tv_sec = TIMEOUT_SEC * 5;
        timeout.tv_usec = 0;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        int ready = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            perror("Select failed");
            requestPacket.service_port = -1;
            return requestPacket;
        } else if (ready == 0) {
            attempts++;
            continue;
        }

        // listen for incoming packets from the registry_server
        BroadcastPacket receivedPacket;
        socklen_t addr_len = sizeof(p);
        ssize_t data_len =
            recvfrom(sockfd, &receivedPacket, sizeof(BroadcastPacket), 0,
                     (struct sockaddr *)&p, &addr_len);
        if (data_len < 0) {
            perror("receive error");
            requestPacket.service_port = -1;
            return requestPacket;
        }

        printf(
            "received server info from server_registry: service_ip=%s, "
            "service_port=%d",
            receivedPacket.service_ip, receivedPacket.service_port);

        freeaddrinfo(servinfo);
        close(sockfd);
    }

    return requestPacket;
}

/*
 * listenForServiceBroadcast - listens for a broadcast from an advertising
 * service
 * returns BroadcastPacket - populated with values if success
 * returns BroadcastPacket - populated with -1 values if failure
 */
BroadcastPacket listenForServiceBroadcast() {
    BroadcastPacket receivedPacket;
    receivedPacket.service_port = -1;
    int broadcast_sock;
    struct sockaddr_in recvAddr;
    // create the broadcast socket
    broadcast_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    // Set up the receiver address
    memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(atoi(SERVICE_DISCOVERY_PORT));
    recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);  // Listen on all interfaces
    // Bind the socket to the broadcast port
    bind(broadcast_sock, (struct sockaddr *)&recvAddr, sizeof(recvAddr));

    // use a receive loop to listen for the server's broadcast
    while (1) {
        printf("Listening for server broadcast...\n");
        ssize_t bytesRead = recvfrom(broadcast_sock, &receivedPacket,
                                     sizeof(receivedPacket), 0, NULL, NULL);
        if (bytesRead < 0) {
            perror("receive error");
            continue;
        }
        printf("Received service broadcast from %s:%d\n",
               receivedPacket.service_ip, receivedPacket.service_port);
        // check if the received broadcast packet is from the server
        if (receivedPacket.service_port != -1) {
            return receivedPacket;
        }
    }

    close(broadcast_sock);
    return receivedPacket;
}

/* allConnectionsFinished - check if all file transmissions have completed
    returns 1 - all file transmissions are complete
    returns 0 - there are still unfinished transmissions
*/
int allConnectionsFinished() {
    for (int i = 0; i < MAX_FILES; i++) {
        if (connections[i].finished != 1) {
            return 0;
        }
    }
    return 1;
}

/*
 * getRandomFileIndex - returns a random file index from the remaining files
 * that have not finished transmission
 */
int getRandomFileIndex(Connection connections[]) {
    int unfinishedFiles[MAX_FILES];
    int unfinishedCount = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        if (connections[i].finished != 1) {
            unfinishedFiles[unfinishedCount] = i;
            unfinishedCount++;
        }
    }

    if (unfinishedCount == 0) {
        return -1;  // all files have finished transmitting
    }

    int randomIndex = rand() % unfinishedCount;
    return unfinishedFiles[randomIndex];
}

/*
 * getConnection - returns a connection by file_index
 */
Connection *getConnection(int file_index) {
    if (file_index >= 0 && file_index < MAX_FILES) {
        return &connections[file_index];
    } else {
        return NULL;
    }
}

/*
 * initConnection - initializes a new connection at the index provided
 */
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
 * copyLineToPacket - copies the next un-transmitted line from a file to a data
 * Packet. This will copy a random number of lines from 1-3 (inclusive).
 */
int copyLineToPacket(FILE *file, Packet *packet, int current_index) {
    char line[MAXBUFLEN];
    int i = 0;

    int line_offset = (rand() % 3);  // copy over 1-3 lines

    while (fgets(line, sizeof(line), file) != NULL) {
        if (i >= current_index && i <= (current_index + line_offset)) {
            // Ensure the line can fit within packet->buffer
            if (strlen(line) < sizeof(packet->buffer)) {
                strcat(packet->buffer, line);
            } else {
                perror("Line too long for buffer.\n");
                return 0;
            }
        }
        i++;
    }
    return current_index + line_offset;
}

/*
 * generatePayload - populates the Packet structure with metadata and data
 */
void generatePayload(Connection *connection, Packet *packet) {
    packet->file_size = connection->file_size;
    packet->file_index = connection->file_index;
    packet->line_index = connection->line_index;
    packet->ack = 0;
    packet->nack = 0;

    /* copy line data from the file to the packet */
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "./client_files/quote%d.txt",
             packet->file_index);
    FILE *file = fopen(file_path, "r");
    int new_offset = copyLineToPacket(file, packet, connection->line_index);
    fclose(file);

    // for server to track what line_index to expect next
    packet->line_end_index = new_offset;
    // compute checksum
    packet->checksum = getChecksum(packet->buffer);

    // detect if finished and set the connection to finished
    if (packet->line_end_index >= connection->file_size - 1)
        connection->finished = 1;
    // update the current transmission line_index for the file
    connection->line_index = new_offset + 1;
}

/* writeBufferToFile - writes a char buffer to concatenated.txt */
void writeBufferToFile(char *buffer) {
    FILE *file = fopen("./client_files/concatenated.txt", "a");
    if (file == NULL) {
        perror("File open error");
        return;
    }
    size_t buffer_size = strlen(buffer);
    buffer[buffer_size - 1] = '\0';  // null-terminate
    size_t bytes_written = fwrite(buffer, 1, buffer_size, file);
    if (bytes_written != buffer_size) {
        perror("File write error");
        fclose(file);
        return;
    }

    fclose(file);
    return;
}

/*
 * receiveWithTimeout - listens on a timeout for a Packet from the server
 *  returns -1 - failure or timeout
 *  returns  0 - damaged data, should send NACK
 *  returns  1 - data received, should send ACK
 */
int receiveWithTimeout(int sockfd, struct addrinfo *p) {
    fd_set read_fds;
    struct timeval timeout;

    // setup timeout timer, longer timeout to give server some time
    timeout.tv_sec = TIMEOUT_SEC * 10;
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
        return -1;  // TIMED-OUT!
    }

    // listen for incoming packets from the server
    Packet receivedPacket;
    socklen_t addr_len = sizeof(p);
    ssize_t data_len = recvfrom(sockfd, &receivedPacket, sizeof(Packet), 0,
                                (struct sockaddr *)&p, &addr_len);
    if (data_len < 0) {
        perror("receive error");
        return -1;
    }

    // Check for damaged data and ask for re-tx if needed
    if (isDamagedPacket(&receivedPacket)) {
        return 0;
    } else {
        writeBufferToFile(receivedPacket.buffer);
        return 1;
    }
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    Packet packets[100];  // for unordered packet delivery
    int packetIndex = 0;  // for unordered packet delivery

    /* client initialization */
    cleanupClientFiles();
    srand(time(NULL));
    // explicitly set all connections as un-initialized
    for (int i = 0; i < MAX_FILES; i++) {
        connections[i].initialized = 0;
    }

    /* handle help and opts */
    int simulateUnorderedPackets = 0;
    int service_discovery_enabled = 0;
    int registry_server_enabled = 0;
    if (argc < 2) {
        fprintf(stderr,
                "usage: ./client SERVER_IP [options]\nor\n"
                "       ./client --enable-service-discovery");
        exit(1);
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            clientPrintUsage();
            exit(0);
        } else if (strcmp(argv[i], "--simulate-unordered-packets") == 0) {
            simulateUnorderedPackets = 1;
            // Initialize the packets
            for (int i = 0; i < 100; i++) {
                clearPacketBuffer(&packets[i]);
                packets[i].file_size = -1;  // set to an invalid value
            }
        } else if (strcmp(argv[i], "--enable-service-discovery") == 0) {
            service_discovery_enabled = 1;
        } else if (strcmp(argv[i], "--use-registry-server") == 0) {
            registry_server_enabled = 1;
        }
    }
    printf(
        "simulateUnorderedPackets: %d, service_discovery_enabled: %d, "
        "registry_server_enabled: %d\n",
        simulateUnorderedPackets, service_discovery_enabled,
        registry_server_enabled);
    if (registry_server_enabled == 1 && service_discovery_enabled == 1) {
        printf(
            "Cannot use the following options at the same time: "
            "--enable-service-discovery, --use-registry-server\n");
        clientPrintUsage();
        exit(0);
    }

    /* socket configuration */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;  // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;

    char server_ip[INET_ADDRSTRLEN];
    char server_port[6];  // assume a port is 5 chars or less
    if (service_discovery_enabled == 1) {
        // try to find broadcasted IP Address and PORT #
        BroadcastPacket service_info = listenForServiceBroadcast();
        if (service_info.service_port != -1) {
            strncpy(server_ip, service_info.service_ip, sizeof(server_ip) - 1);
            snprintf(server_port, sizeof(server_port), "%d",
                     service_info.service_port);
        }
    } else if (registry_server_enabled == 1) {
        BroadcastPacket service_info = getServiceFromRegistry();
        if (service_info.service_port != -1) {
            strncpy(server_ip, service_info.service_ip, sizeof(server_ip) - 1);
            snprintf(server_port, sizeof(server_port), "%d",
                     service_info.service_port);
        } else if (service_info.service_port == -1) {
            printf(
                "Error or timeout finding server info from registry_server\n");
            exit(0);
        }
    } else {
        // use command line arg for IP Address and Default PORT #
        strncpy(server_ip, argv[1], sizeof(server_ip) - 1);
        strncpy(server_port, SERVERPORT, sizeof(server_port) - 1);
    }

    if ((rv = getaddrinfo(server_ip, server_port, &hints, &servinfo)) != 0) {
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

    /* main client transmission loop */
    while (allConnectionsFinished() != 1) {
        Packet *packet = malloc(sizeof(Packet));

        int randomFileIndex = getRandomFileIndex(connections);
        if (randomFileIndex == -1) break;

        /* get the current connection or establish a new one */
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

        /* if the --simulate-unordered-packets flag is set */
        if (simulateUnorderedPackets == 1) {
            // Generate the payload for the current connection
            clearPacketBuffer(&packets[packetIndex]);  // Clear the buffer
            generatePayload(connection, &packets[packetIndex]);
            // Move to the next index for the next packet
            packetIndex++;
            continue;
        }

        /* generate and send data */
        generatePayload(connection, packet);
        int success = sendAndWaitForACK(sockfd, p, packet);
        if (success == 0) {
            printf(
                "Max Retransmissions hit, double-check server "
                "connection\n");
            exit(1);
        }

        /* cleanup */
        clearPacketBuffer(packet);
        free(packet);
        printf("\n");
    }

    /* if the --simulate-unordered-packets flag is set */
    if (simulateUnorderedPackets == 1) {
        // Shuffle the packets
        for (int i = packetIndex - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            Packet temp = packets[i];
            packets[i] = packets[j];
            packets[j] = temp;
        }

        // send the shuffled packets
        for (int i = 0; i < packetIndex; i++) {
            if (packets[i].file_size != -1) {
                int success = sendAndWaitForACK(sockfd, p, &packets[i]);
                if (success == 0) {
                    printf(
                        "Max Retransmissions hit, double-check server "
                        "connection\n");
                    exit(1);
                }
            }
        }
    }

    /* receive concatenated files from server */
    int retransmissions = 0;
    while (retransmissions < MAX_RETRANSMISSIONS) {
        int result = receiveWithTimeout(sockfd, p);
        if (result == 1) {
            printf("client: sending a ACK\n");
            sendServerACK(sockfd, p);
            break;
        } else if (result == 0) {
            printf("client: sending a NACK\n");
            sendServerNACK(sockfd, p);
            retransmissions++;
        } else {
            break;
        }
    }

    /* cleanup */
    freeaddrinfo(servinfo);
    close(sockfd);

    return 0;
}
