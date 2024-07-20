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

#include "../inc/frame.h"
#include "../inc/raw-socket.h"

using namespace std;

string translate_frame_type(uint8_t type) {
	switch (type) {
		case TYPE_ACK:
			return "ACK";
		case TYPE_NACK:
			return "NACK";
		case TYPE_LIST:
			return "LIST";
		case TYPE_DOWNLOAD:
			return "DOWNLOAD";
		case TYPE_SHOWS_ON_SCREEN:
			return "SHOWS ON SCREEN";
		case TYPE_FILL_DESCRIPTOR:
			return "FILL DESCRIPTOR";
		case TYPE_DATA:
			return "DATA";
		case TYPE_END_TX:
			return "END TX";
		case TYPE_ERROR:
			return "ERROR";
		default:
			return "UNKNOWN";
	}
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

bool send_frame_and_receive_ack(int sockfd, Frame &frame, int timeout_seconds) {
	auto start = chrono::steady_clock::now();
	uint8_t buffer[sizeof(Frame)] = {0};

	safe_send(sockfd, reinterpret_cast<uint8_t *>(&frame), sizeof(Frame));
	cout << "Sent frame " << (int)frame.sequence << " (" << translate_frame_type(frame.type) << ")"
		 << endl;

	while (true) {
		// Check elapsed time
		ssize_t len = safe_recv(sockfd, buffer, sizeof(Frame));
		auto now = chrono::steady_clock::now();
		auto elapsed_seconds = chrono::duration_cast<chrono::seconds>(now - start).count();

		if (len > 0) {
			// Received a package, check if it is what was expected
			Frame *response = reinterpret_cast<Frame *>(buffer);
			if (response->start_marker == START_MARKER && response->sequence == frame.sequence) {
				// Checks CRC
				unsigned char crc = calculate_crc(*response);
				if (crc == response->crc) {
					cout << "Receive frame " << (int)response->sequence << " ("
						 << translate_frame_type(response->type) << ")" << endl;
					if (response->type == TYPE_ACK) {
						return true;
					} else {
						cout << "CRC check failed" << endl;
					}
				}
			}

			// Timout
			if (elapsed_seconds >= timeout_seconds) {
				cout << "Own timeout reached, no valid response." << endl;
				return false;
			}
		}
	}
	return false;
}

bool receive_frame_with_timeout(int sockfd, Frame &frame, int timeout_seconds) {
	auto start = chrono::steady_clock::now();
	uint8_t buffer[sizeof(Frame)] = {0};

	while (true) {
		ssize_t len = safe_recv(sockfd, buffer, sizeof(Frame));

		if (len > 0) {
			// Received a package, check if it is what was expected
			Frame *response = reinterpret_cast<Frame *>(buffer);
			if (response->start_marker == START_MARKER) {
				// Checks CRC
				unsigned char crc = calculate_crc(*response);
				if (crc == response->crc) {
					cout << "Receive frame " << (int)response->sequence << " ("
						 << translate_frame_type(response->type) << ")" << endl;
					memcpy(&frame, buffer, sizeof(Frame));
					return true;
				} else {
					cout << "CRC error in received frame " << (int)frame.sequence << endl;
					frame.type = TYPE_NACK;
					safe_send(sockfd, reinterpret_cast<uint8_t *>(&frame), sizeof(Frame));
					return false;
				}
			}
		}

		auto now = chrono::steady_clock::now();
		auto elapsed_seconds = chrono::duration_cast<chrono::seconds>(now - start).count();
		// Timout
		if (elapsed_seconds >= timeout_seconds) {
			cout << "Own timeout reached, no valid response." << endl;
			return false;
		}
	}
}

bool receive_frame_and_send_ack(int sockfd, Frame &frame, int timeout_seconds) {
	if (receive_frame_with_timeout(sockfd, frame, timeout_seconds)) {
		uint8_t sequence = frame.sequence;
		send_ack(sockfd, sequence);
		return true;
	}
	return false;
}

void send_ack(int sockfd, uint8_t sequence) {
	Frame ack;
	ack.start_marker = START_MARKER;
	ack.length = 0;
	ack.sequence = sequence;
	ack.type = TYPE_ACK;
	ack.crc = calculate_crc(ack);

	safe_send(sockfd, reinterpret_cast<uint8_t *>(&ack), sizeof(Frame));

	cout << "Sent frame " << (int)sequence << " (" << translate_frame_type(ack.type) << ")" << endl;
}

void send_nack(int sockfd, uint8_t sequence) {
	Frame nack;
	nack.start_marker = START_MARKER;
	nack.length = 0;
	nack.sequence = sequence;
	nack.type = TYPE_NACK;
	nack.crc = calculate_crc(nack);

	safe_send(sockfd, reinterpret_cast<uint8_t *>(&nack), sizeof(Frame));
	cout << "Sent frame " << (int)sequence << " (" << translate_frame_type(nack.type) << ")"
		 << endl;
}

bool receive_ack(int sockfd, uint8_t &ack_sequence, int timeout_seconds) {
	Frame frame;
	if (receive_frame_with_timeout(sockfd, frame, timeout_seconds)) {
		if (frame.type == TYPE_ACK) {
			ack_sequence = frame.sequence;
			return true;
		}
	}
	return false;
}

void send_file(int sockfd, ifstream &file, int timeout_seconds) {
	vector<Frame> window(WINDOW_SIZE);
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

			safe_send(sockfd, reinterpret_cast<uint8_t *>(&frame), sizeof(Frame));
			cout << "Sent frame " << (int)frame.sequence << " (" << translate_frame_type(frame.type)
				 << ")" << endl;
			next_seq_num = (next_seq_num + 1) % 32;
		}
		expected_ack = next_seq_num;
		last_ack_received = 0;

		uint8_t base = 0;
		while (last_ack_received < expected_ack - 1) {
			if (receive_ack(sockfd, last_ack_received, timeout_seconds)) {
				if (last_ack_received < WINDOW_SIZE) {
					base = (last_ack_received + 1) % 32;
				}
			} else {
				cout << "NACK, resending frames from " << (int)base << " to " << WINDOW_SIZE
					 << endl;
				for (uint8_t i = base; i < WINDOW_SIZE; i = (i + 1) % 32) {
					Frame &frame = window[i];
					safe_send(sockfd, reinterpret_cast<uint8_t *>(&frame), sizeof(Frame));
					cout << "Resent frame " << (int)frame.sequence << " ("
						 << translate_frame_type(frame.type) << ")" << endl;
				}
			}
		}
	}
	next_seq_num = next_seq_num % WINDOW_SIZE;

	/* Sends the end of transmission frame */
	Frame end_frame;
	end_frame.start_marker = START_MARKER;
	end_frame.sequence = next_seq_num;
	end_frame.type = TYPE_END_TX;
	end_frame.length = 0;
	end_frame.crc = calculate_crc(end_frame);
	last_ack_received = 0;

	/* Sent last frame and receive ACK */
	if (send_frame_and_receive_ack(sockfd, end_frame, timeout_seconds)) {
		cout << "Sent end of transmission frame " << (int)end_frame.sequence << endl;
	}
}
