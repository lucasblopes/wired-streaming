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
	struct sockaddr_ll server_addr;

	sockfd = create_raw_socket();

	// Obtain the interface index for the specified network interface
	const char *interfaceName = INTERFACE_NAME;
	int interface_index = if_nametoindex(interfaceName);
	if (interface_index == 0) {
		perror(
			"Error obtaining interface index: Check if the interface name "
			"is correct.");
		exit(1);
	}

	server_addr.sll_family = AF_PACKET;
	server_addr.sll_protocol = htons(ETH_P_ALL);
	server_addr.sll_ifindex = interface_index;

	socket_config(sockfd, TIMEOUT_SECONDS, interface_index);

	vector<Frame> window(WINDOW_SIZE);
	int base = 0;
	int next_seq = 0;
	int total_frames = 10;

	while (base < total_frames) {
		// Send frames within the window
		for (int i = 0; i < WINDOW_SIZE && next_seq < base + WINDOW_SIZE && next_seq < total_frames;
			 ++i) {
			Frame &frame = window[next_seq % WINDOW_SIZE];
			frame.start_marker = 0x7E;
			frame.length = 5;
			frame.sequence = next_seq;
			frame.type = 0xA;
			strcpy((char *)frame.data, "Hello");
			frame.crc = calculate_crc(frame);

			sendto(sockfd, &frame, sizeof(Frame), 0, (struct sockaddr *)&server_addr,
				   sizeof(server_addr));
			cout << "Sent frame " << hex << setw(2) << setfill('0') << (int)frame.sequence << endl;
			next_seq++;
		}

		// Receive ACKs and handle timeouts
		for (int i = base; i < next_seq; ++i) {
			Frame ack;
			if (receive_frame_with_timeout(sockfd, server_addr, ack, TIMEOUT_SECONDS)) {
				if (ack.type == 0 && ack.sequence >= base) {
					base = ack.sequence + 1;
				}
			} else {
				// Timeout, retransmit frames starting from the first unacknowledged frame
				cout << "Timeout occurred, retransmitting frames starting from sequence: " << base
					 << endl;
				next_seq = base;
				break;
			}
		}
	}

	close(sockfd);
	return 0;
}
