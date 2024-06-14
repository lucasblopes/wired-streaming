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

using namespace std;

int main(int argc, char *argv[]) {
	if (argc != 2) {
		cerr << "Usage: " << argv[0] << " <file_path>" << endl;
		return 1;
	}
	const char *file_path = argv[1];

	ifstream file(file_path, ios::binary);
	if (!file) {
		cerr << "Failed to open file: " << file_path << endl;
		return 1;
	}

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

	send_file(sockfd, server_addr, file);

	close(sockfd);
	return 0;
}
