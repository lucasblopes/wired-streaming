#include "../inc/frame.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>

using namespace std;

int create_raw_socket() {
	int sockfd;
	if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	return sockfd;
}

/* calculates crc8 */
uint8_t calculate_crc(const Frame &frame) {
	uint8_t crc = 0;
	crc ^= frame.start_marker;	// XOR
	crc ^= frame.length;
	crc ^= frame.sequence;
	crc ^= frame.type;
	for (int i = 0; i < frame.length; ++i) {
		crc ^= frame.data[i];
	}
	return crc;
}

/* set timeout limit and promiscuous mode */
void socket_config(int sockfd, int timeout_seconds, int interface_index) {
	// set timeout limit
	struct timeval timeout;
	timeout.tv_sec = timeout_seconds;
	timeout.tv_usec = 0;
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
		perror("setsockopt failed");
		exit(EXIT_FAILURE);
	}
	// Set the socket to promiscuous mode to capture all packets
	struct packet_mreq mr = {};
	mr.mr_ifindex = interface_index;
	mr.mr_type = PACKET_MR_PROMISC;

	if (setsockopt(sockfd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1) {
		perror("setsockopt failed: Ensure the network interface is specified correctly");
		exit(EXIT_FAILURE);
	}
}

// Function to send a frame and wait for a response with its own timeout/ Função para enviar um
// frame e aguardar resposta com timeout próprio
bool send_frame_with_timeout(int sockfd, Frame &frame, struct sockaddr_ll &addr, int addrlen,
							 int timeout_seconds) {
	auto start = chrono::steady_clock::now();
	char buffer[sizeof(Frame)] = {0};

	while (true) {
		// Send the frame
		sendto(sockfd, &frame, sizeof(Frame), 0, (struct sockaddr *)&addr, addrlen);
		cout << "Frame '" << frame.data << "' sent, waiting for response..." << endl;

		// Wait for response with timeout on socketsposta com timeout no socket
		ssize_t len = recvfrom(sockfd, buffer, sizeof(Frame), 0, (struct sockaddr *)&addr,
							   (socklen_t *)&addrlen);

		// Check elapsed time
		auto now = chrono::steady_clock::now();
		auto elapsed_seconds = chrono::duration_cast<chrono::seconds>(now - start).count();

		// If there was an error and it was not a socket timeout, we exit with failure
		if (len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// Socket timeout, check own timeout
				if (elapsed_seconds >= timeout_seconds) {
					cerr << "Own timeout reached, no valid response." << endl;
					return false;
				}
				// Continue waiting
			} else {
				perror("recvfrom failed");
				return false;
			}
		} else {
			// Received a package, check if it is what was expected
			Frame *response = reinterpret_cast<Frame *>(buffer);
			if (response->start_marker == 0x7E && response->sequence == frame.sequence) {
				// Checks CRC
				unsigned char crc = calculate_crc(*response);
				if (crc == response->crc) {
					cout << "Valid response received: " << response->data << endl;
					return true;
				} else {
					cerr << "CRC check failed" << endl;
				}
			}
		}

		// Timout
		if (elapsed_seconds >= timeout_seconds) {
			cerr << "Own timeout reached, no valid response." << endl;
			return false;
		}
	}
}

bool receive_frame_with_timeout(int sockfd, struct sockaddr_ll &addr, Frame &frame,
								int timeout_seconds) {
	socklen_t addr_len = sizeof(addr);
	auto start = chrono::steady_clock::now();
	char buffer[sizeof(Frame)] = {0};

	while (true) {
		ssize_t len =
			recvfrom(sockfd, buffer, sizeof(Frame), 0, (struct sockaddr *)&addr, &addr_len);

		// Check elapsed time
		auto now = chrono::steady_clock::now();
		auto elapsed_seconds = chrono::duration_cast<chrono::seconds>(now - start).count();

		if (len > 0) {
			// Received a package, check if it is what was expected
			Frame *response = reinterpret_cast<Frame *>(buffer);
			if (response->start_marker == 0x7E) {
				// Checks CRC
				unsigned char crc = calculate_crc(*response);
				if (crc == response->crc) {
					// CRC is correct, copies the buffer to the frame structure
					memcpy(&frame, buffer, sizeof(Frame));
					cout << "Valid response received: " << response->data << endl;
					return true;
				} else {
					cerr << "CRC check failed" << endl;
				}
			}
		} else if (len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// Socket timeout, check own timeout
				if (elapsed_seconds >= timeout_seconds) {
					cerr << "Own timeout reached, no valid response." << endl;
					return false;
				}
				// Keep waiting
			} else {
				perror("recvfrom failed");
				return false;
			}
		}

		// Timout
		if (elapsed_seconds >= timeout_seconds) {
			cerr << "Own timeout reached, no valid response." << endl;
			return false;
		}
	}
}
