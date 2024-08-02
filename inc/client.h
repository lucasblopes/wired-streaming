#ifndef CLIENT_H
#define CLIENT_H

#endif

#include <arpa/inet.h>
#include <dirent.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#include <random>
#include <unordered_set>
#include <chrono>
#include <thread>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "../inc/frame.h"
#include "../inc/raw-socket.h"

bool receive_file(int sockfd, ofstream &file, int timeout_seconds);

vector<string> list_files(int sockfd, int timeout_seconds);

void download_file(int sockfd, const string &filename, int timeout_seconds);