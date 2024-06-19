#include "../inc/frame.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <vector>

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
		perror(
			"setsockopt failed: Ensure the network interface is specified "
			"correctly");
		exit(EXIT_FAILURE);
	}
}

bool send_frame_and_receive_ack(int sockfd, Frame &frame, struct sockaddr_ll &addr,
								int timeout_seconds) {
	socklen_t addrlen = sizeof(addr);
	sendto(sockfd, &frame, sizeof(Frame), 0, (struct sockaddr *)&addr, addrlen);
	cout << "Sent frame " << (int)frame.sequence << " (" << (int)frame.type << ")" << endl;
	uint8_t sequence = frame.sequence;
	return (receive_ack(sockfd, addr, sequence, timeout_seconds));
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
			if (response->start_marker == START_MARKER) {
				// Checks CRC
				unsigned char crc = calculate_crc(*response);
				if (crc == response->crc) {
					// CRC is correct, copies the buffer to the frame structure
					cout << "Received frame " << (int)response->sequence << " ("
						 << (int)response->type << ")" << endl;
					memcpy(&frame, buffer, sizeof(Frame));
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

bool receive_frame_and_send_ack(int sockfd, struct sockaddr_ll &addr, Frame &frame,
								int timeout_seconds) {
	if (receive_frame_with_timeout(sockfd, addr, frame, timeout_seconds)) {
		uint8_t sequence = frame.sequence;
		send_ack(sockfd, addr, sequence);
		return true;
	}
	return false;
}

void send_ack(int sockfd, struct sockaddr_ll &client_addr, uint8_t sequence) {
	Frame ack;
	ack.start_marker = START_MARKER;
	ack.length = 0;
	ack.sequence = sequence;
	ack.type = TYPE_ACK;
	ack.crc = calculate_crc(ack);

	sendto(sockfd, &ack, sizeof(Frame), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
	cout << "Sent frame " << (int)sequence << " (" << (int)ack.type << ")" << endl;
}

void send_nack(int sockfd, struct sockaddr_ll &client_addr, uint8_t sequence) {
	Frame nack;
	nack.start_marker = START_MARKER;
	nack.length = 0;
	nack.sequence = sequence;
	nack.type = TYPE_NACK;
	nack.crc = calculate_crc(nack);

	sendto(sockfd, &nack, sizeof(Frame), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
	cout << "Sent frame " << (int)sequence << " (" << (int)nack.type << ")" << endl;
}

bool receive_ack(int sockfd, struct sockaddr_ll &addr, uint8_t &ack_sequence, int timeout_seconds) {
	Frame frame;
	if (receive_frame_with_timeout(sockfd, addr, frame, timeout_seconds)) {
		if (frame.type == TYPE_ACK) {
			ack_sequence = frame.sequence;
			return true;
		}
	}
	return false;
}

void send_file(int sockfd, struct sockaddr_ll &addr, ifstream &file) {
	vector<Frame> window(WINDOW_SIZE);
	int addr_len = sizeof(addr);
	uint8_t next_seq_num = 0;
	uint8_t expected_ack;
	uint8_t last_ack_received;

	while (!file.eof()) {
		next_seq_num = 0;
		while (next_seq_num < WINDOW_SIZE && !file.eof()) {
			Frame &frame = window[next_seq_num % WINDOW_SIZE];
			frame.start_marker = START_MARKER;
			frame.sequence = next_seq_num % WINDOW_SIZE;
			frame.type = TYPE_DATA;
			file.read((char *)frame.data, sizeof(frame.data));
			frame.length = file.gcount();
			frame.crc = calculate_crc(frame);

			sendto(sockfd, &frame, sizeof(Frame), 0, (struct sockaddr *)&addr, addr_len);
			cout << "Sent frame " << (int)frame.sequence << " (" << (int)frame.type << ")" << endl;
			next_seq_num = (next_seq_num + 1) % 32;
		}
		expected_ack = next_seq_num;
		last_ack_received = 0;

		uint8_t base = 0;
		while (last_ack_received < expected_ack - 1) {
			if (receive_ack(sockfd, addr, last_ack_received, TIMEOUT_SECONDS)) {
				if (last_ack_received < WINDOW_SIZE) {
					base = (last_ack_received + 1) % 32;
				}
			} else {
				cout << "NACK, resending frames from " << (int)base << " to " << WINDOW_SIZE
					 << endl;
				for (uint8_t i = base; i < WINDOW_SIZE; i = (i + 1) % 32) {
					Frame &frame = window[i];
					sendto(sockfd, &frame, sizeof(Frame), 0, (struct sockaddr *)&addr, addr_len);
					cout << "Resent frame " << (int)frame.sequence << " (" << (int)frame.type << ")"
						 << endl;
				}
			}
		}
	}

	/* Sends the end of transmission frame */
	Frame end_frame;
	end_frame.start_marker = START_MARKER;
	end_frame.sequence = next_seq_num;
	end_frame.type = TYPE_END_TX;
	end_frame.length = 0;
	end_frame.crc = calculate_crc(end_frame);
	last_ack_received = 0;

	/* Sent last frame and receive ACK */
	if (send_frame_and_receive_ack(sockfd, end_frame, addr, TIMEOUT_SECONDS)) {
		cout << "Sent end of transmission frame " << (int)end_frame.sequence << endl;
	}
}
