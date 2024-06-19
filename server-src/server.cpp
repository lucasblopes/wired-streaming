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

	ofstream output_file("received_file.mp4", ios::binary);
	if (!output_file) {
		cerr << "Failed to open output file." << endl;
		return 1;
	}

	uint8_t expected_sequence = 0;

	while (true) {
		Frame frame;
		struct sockaddr_ll client_addr;

		if (receive_frame_and_send_ack(sockfd, client_addr, frame, TIMEOUT_SECONDS)) {
			if (frame.sequence == expected_sequence) {
				if (frame.type == TYPE_END_TX) {
					output_file.close();
					cout << "Received end of transmission frame." << endl;
					break;
				} else if (frame.type == TYPE_DATA) {
					output_file.write((char *)frame.data, frame.length);
				} else {
					send_nack(sockfd, client_addr, expected_sequence);
				}
				expected_sequence = (expected_sequence + 1) % WINDOW_SIZE;
			}
		}
	}

	close(sockfd);
	return 0;
}
