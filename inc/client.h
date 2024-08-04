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

#include "frame.h"
#include "raw-socket.h"
#include "config.h"

// Request files available for download in server and print them
vector<string> list_files(int sockfd, int timeout_seconds);

// Download file from server
void download_file(int sockfd, const string &filename, int timeout_seconds);