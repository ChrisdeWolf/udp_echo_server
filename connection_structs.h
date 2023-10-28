#ifndef CONNECTION_STRUCTS_H
#define CONNECTION_STRUCTS_H

#define MAX_BUF_LEN 1024
typedef struct {
    int file_size;             // total lines in the file
    int file_index;            // file index 0-9 (effectively the filename)
    int line_index;            // current line index 0-file_size-1
    int line_end_index;        // index of last line sent:
                               //   line_end_index+1 = next_expected_line_index
    unsigned short checksum;   // checksum for identifying errors
    char buffer[MAX_BUF_LEN];  // data for that line
} Packet;

typedef struct {
    int initialized;  // bool - is connection initialized
    int finished;     // bool - is connection finished
    int file_size;    // total lines in the file
    int file_index;   // file index 0-9 (effectively the filename)
    int line_index;   // current line index 0-file_size-1
} Connection;

#define MAX_LINES 100  // max lines a file is allowed to have TODO: do i need?
// File buffer structure for out-of-order packets
typedef struct {
    Packet buffer[MAX_LINES];
    int next_expected_line_index;
} FileBuffer;

unsigned short getChecksum(const char* data) {
    unsigned int sum = 0;
    int i;
    for (i = 0; i < MAX_BUF_LEN; i++) {
        sum += (unsigned char)data[i];
    }
    return (unsigned short)(sum & 0xFFFF);
}

#endif
