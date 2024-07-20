
#include <arpa/inet.h>
#include <dirent.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace std;
#include "../inc/raw-socket.h"

// Creates a raw socket
int create_socket() {
	int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sockfd == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	return sockfd;
}

// Gets the index of the specified network interface
int get_interface_index(const char *interface_name) {
	int interface_index = if_nametoindex(interface_name);
	if (interface_index == 0) {
		perror("Error obtaining interface index: Check if the interface name is correct.");
		exit(EXIT_FAILURE);
	}
	return interface_index;
}

// Binds the socket to the specified network interface
void bind_socket_to_interface(int sockfd, int interface_index) {
	struct sockaddr_ll address;
	memset(&address, 0, sizeof(address));
	address.sll_family = AF_PACKET;
	address.sll_protocol = htons(ETH_P_ALL);
	address.sll_ifindex = interface_index;

	if (bind(sockfd, (struct sockaddr *)&address, sizeof(address)) == -1) {
		perror("bind");
		close(sockfd);
		exit(EXIT_FAILURE);
	}
}

// Sets the socket to promiscuous mode
void set_socket_promiscuous(int sockfd, int interface_index) {
	struct packet_mreq mr;
	memset(&mr, 0, sizeof(mr));
	mr.mr_ifindex = interface_index;
	mr.mr_type = PACKET_MR_PROMISC;

	if (setsockopt(sockfd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1) {
		perror("setsockopt failed: Ensure the network interface is specified correctly");
		close(sockfd);
		exit(EXIT_FAILURE);
	}
}

// Sets the timeout for socket operations
void set_socket_timeout(int sockfd, int timeout_seconds) {
	struct timeval timeout;
	timeout.tv_sec = timeout_seconds;
	timeout.tv_usec = 0;

	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
		perror("setsockopt failed");
		close(sockfd);
		exit(EXIT_FAILURE);
	}
}

// Creates and configures a raw socket
int raw_socket_create(const char *interface_name, int timeout_seconds) {
	int sockfd = create_socket();
	int interface_index = get_interface_index(interface_name);
	bind_socket_to_interface(sockfd, interface_index);
	set_socket_promiscuous(sockfd, interface_index);
	set_socket_timeout(sockfd, timeout_seconds);

	return sockfd;
}

ssize_t safe_send(int sockfd, uint8_t *data, size_t length) {
	vector<uint8_t> escaped_data;
	escaped_data.reserve(length * 2);  // Reserve enough space to handle all potential escapes

	for (size_t i = 0; i < length; ++i) {
		escaped_data.push_back(data[i]);
		if (data[i] == 0x88 || data[i] == 0x81) {
			escaped_data.push_back(0xff);  // Add escape byte
		}
	}

	// Send the escaped data
	ssize_t bytes_sent = send(sockfd, escaped_data.data(), escaped_data.size(), 0);
	if (bytes_sent == -1) {
		perror("send");
		// Handle error appropriately
	}

	return bytes_sent;
}

ssize_t safe_recv(int sockfd, uint8_t *buffer, size_t length) {
	vector<uint8_t> received_data(length * 2);	// Allocate enough space for potential escape bytes

	// Receive the data
	ssize_t bytes_received = recv(sockfd, received_data.data(), received_data.size(), 0);
	if (bytes_received == -1) {
		return -1;
	}

	size_t j = 0;
	for (ssize_t i = 0; i < bytes_received; ++i) {
		if (received_data[i] == 0x88 || received_data[i] == 0x81) {
			if (i + 1 < bytes_received && received_data[i + 1] == 0xff) {
				// Skip the escape byte
				++i;
			}
		}
		buffer[j++] = received_data[i];
		// Ensure we don't overflow the buffer
		if (j >= length) {
			break;
		}
	}

	return j;  // Return the actual number of bytes after removing escape bytes
}
