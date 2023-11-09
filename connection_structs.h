/*
 *   Author: Christopher deWolf
 *   connection_structs.h -- shared constants and data structures used by
 *   clients/server
 */

#ifndef CONNECTION_STRUCTS_H
#define CONNECTION_STRUCTS_H

// CONSTANTS
#define SERVERPORT "7777"      // the server port for this file exchange process
#define MAXBUFLEN 8192         // max data size in a packet buffer
#define MAX_RETRANSMISSIONS 3  // max retransmission attempts
#define MAX_FILES 10           // max # of files in an exchange
#define MAX_LINES 100          // max lines a file is allowed to have
#define TIMEOUT_SEC 1          // timeout (seconds) to wait until retransmission
#define SERVICE_DISCOVERY_PORT "8888"
#define BEACON_INTERVAL_SEC 3  // beacon interval in seconds

// DATA STRUCTURES
typedef struct {
    int file_size;            // total lines in the file
    int file_index;           // file index 0-9 (effectively the filename)
    int line_index;           // current line index 0-file_size-1
    int line_end_index;       // index of last line sent:
                              //   line_end_index+1 = next_expected_line_index
    unsigned short checksum;  // checksum for identifying errors
    int ack;                  // acknowledgement, 1=data received
    int nack;                 // negative-acknowledgment, 1=data damaged
    char buffer[MAXBUFLEN];   // data
} Packet;

typedef struct {
    char service_ip[INET_ADDRSTRLEN];  // IP Address of the advertised service
    int service_port;                  // Port # of the service
} BroadcastPacket;

typedef struct {
    int initialized;  // bool - is connection initialized
    int finished;     // bool - is connection finished
    int file_size;    // total lines in the file
    int file_index;   // file index 0-9 (effectively the filename)
    int line_index;   // current line index 0-file_size-1
} Connection;

// File buffer structure used by server to store out-of-order packets
typedef struct {
    Packet buffer[MAX_LINES];      // buffer of packets, indexed by line_index
    int next_expected_line_index;  // index of next packet to be used by server
} FileBuffer;

#endif
