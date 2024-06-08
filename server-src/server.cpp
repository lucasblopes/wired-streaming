#include <arpa/inet.h>
#include <dirent.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iomanip>
#include <iostream>

#include "../inc/frame.h"

using namespace std;

int main() {
	int sockfd;
	struct sockaddr_ll client_addr;

	sockfd = create_raw_socket();

	// Obtain the interface index for the specified network interface
	const char *interface_name = INTERFACE_NAME;
	int interface_index = if_nametoindex(interface_name);
	if (interface_index == 0) {
		perror(
			"Error obtaining interface index: Check if the interface name "
			"is correct.");
		exit(1);
	}

	// Set up the socket address structure
	struct sockaddr_ll address = {};
	address.sll_family = AF_PACKET;
	address.sll_protocol = htons(ETH_P_ALL);
	address.sll_ifindex = interface_index;

	if (bind(sockfd, (struct sockaddr *)&address, sizeof(address)) == -1) {
		perror("bind");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	socket_config(sockfd, TIMEOUT_SECONDS, interface_index);

	while (true) {
		socklen_t addr_len = sizeof(client_addr);
		char buffer[sizeof(Frame)] = {0};

		ssize_t len =
			recvfrom(sockfd, &buffer, sizeof(Frame), 0, (struct sockaddr *)&client_addr, &addr_len);
		/* Frame *response = reinterpret_cast<Frame *>(buffer); */
		Frame *response = reinterpret_cast<Frame *>(buffer);
		if (len > 0 && response->start_marker == 0x7E) {
			// Check CRC
			unsigned char crc = calculate_crc(*response);
			if (crc == response->crc) {
				cout << "Received frame " << hex << setw(2) << setfill('0')
					 << (int)response->sequence << " : " << response->data << endl;

				// Send ACK
				Frame ack;
				ack.start_marker = 0x7E;
				ack.length = 3;
				ack.sequence = response->sequence;
				ack.type = 0;
				strcpy((char *)ack.data, "ACK");
				ack.crc = calculate_crc(ack);

				ssize_t sent_bytes = sendto(sockfd, &ack, sizeof(Frame), 0,
											(struct sockaddr *)&client_addr, sizeof(client_addr));
				if (sent_bytes == -1) {
					cerr << "Failed to send ACK: " << strerror(errno) << endl;
				} else {
					cout << "Sent ACK successfully!" << endl;
				}
			} else {
				cerr << "CRC check failed" << endl;
			}
		}
	}

	close(sockfd);
	return 0;
}
