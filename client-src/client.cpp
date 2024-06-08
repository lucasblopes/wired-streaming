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

	// set the frame
	Frame frame;
	frame.start_marker = 0x7E;
	frame.length = 5;
	frame.sequence = 0;
	frame.type = 0xA;
	strcpy((char *)frame.data, "Hello from client!");
	frame.crc = calculate_crc(frame);

	// Send the frame and wait for the response
	if (send_frame_with_timeout(sockfd, frame, server_addr, sizeof(server_addr), TIMEOUT_SECONDS)) {
		cout << "Frame sent successfully" << endl;
	} else {
		cerr << "Failed to send the frame within the timeout period" << endl;
	}

	close(sockfd);
	return 0;
}
