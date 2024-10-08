#ifndef SOCKET_H
#define SOCKET_H

#include <cstdint>
#include <iostream>

#include "config.h"

using namespace std;

// Creates a raw socket
int create_socket();

// Gets the index of the specified network interface
int get_interface_index(const char *interface_name);

// Binds the socket to the specified network interface
void bind_socket_to_interface(int sockfd, int interface_index);

// Sets the socket to promiscuous mode
void set_socket_promiscuous(int sockfd, int interface_index);

// Sets the timeout for socket operations
void set_socket_timeout(int sockfd, int timeout_seconds);

// Creates and configures a raw socket
int raw_socket_create(const char *interface_name, int timeout_seconds);

#endif
