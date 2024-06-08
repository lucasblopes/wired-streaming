#ifndef FRAME_H
#define FRAME_H

#include <arpa/inet.h>
#include <dirent.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

// modify if necessary
#define INTERFACE_NAME "eno1"
#define TIMEOUT_SECONDS 5
#define START_MARKER 0x7E  // 01111110

struct Frame {
	uint8_t start_marker;
	uint8_t length : 6;
	uint8_t sequence : 5;
	uint8_t type : 5;
	uint8_t data[63];
	uint8_t crc;
};

int create_raw_socket();

uint8_t calculate_crc(const Frame &frame);

void socket_config(int sockfd, int timeout_seconds, int interface_index);

bool send_frame_with_timeout(int sockfd, Frame &frame, struct sockaddr_ll &addr, int addrlen,
							 int timeout_seconds);

bool receive_frame_with_timeout(int sockfd, struct sockaddr_ll &addr, Frame &frame,
								int timeout_seconds);

#endif
