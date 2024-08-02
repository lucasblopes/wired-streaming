#ifndef SERVER_H
#define SERVER_H

#include <arpa/inet.h>
#include <dirent.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "../inc/frame.h"
#include "../inc/raw-socket.h"

using namespace std;

void handle_list_request(int sockfd, int timeout_seconds);

void handle_download_request(int sockfd, const Frame &frame, int timeout_seconds);

void listen_for_requests(int sockfd, Frame &request);

#endif