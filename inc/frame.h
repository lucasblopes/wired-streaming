#ifndef FRAME_H
#define FRAME_H

#include <arpa/inet.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace std;

#define WINDOW_SIZE 5
#define FRAME_DATA_SIZE 63
#define MAX_SEQ 32

#define START_MARKER 0x7E		   // 01111110
#define TYPE_ACK 0x00			   // 00000
#define TYPE_NACK 0x01			   // 00001
#define TYPE_LIST 0x0A			   // 01010
#define TYPE_DOWNLOAD 0x0B		   // 01011
#define TYPE_SHOWS_ON_SCREEN 0x10  // 10000
#define TYPE_FILE_DESCRIPTOR 0x11  // 10001
#define TYPE_DATA 0x12			   // 10010
#define TYPE_END_TX 0x1E		   // 11110
#define TYPE_ERROR 0x1F			   // 11111

struct Frame {
	uint8_t start_marker;
	uint8_t length : 6;
	uint8_t sequence : 5;
	uint8_t type : 5;
	uint8_t data[63];
	uint8_t crc;
};

string translate_frame_type(uint8_t type);

uint8_t calculate_crc(const Frame &frame);

bool send_frame_and_receive_ack(int sockfd, Frame &frame, int timeout_seconds);

bool receive_frame_with_timeout(int sockfd, Frame &frame, int timeout_seconds);

bool receive_frame_and_send_ack(int sockfd, Frame &frame, int timeout_seconds);

void send_ack(int sockfd, uint8_t sequence);

bool receive_ack(int sockfd, uint8_t &ack_sequence, int timeout_seconds);

void send_nack(int sockfd, uint8_t sequence);

void send_file(int sockfd, ifstream &file, int timeout_seconds);

#endif
