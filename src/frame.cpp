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
		case TYPE_FILE_DESCRIPTOR:
			return "FILE DESCRIPTOR";
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

uint32_t calculate_crc32(const void* data, size_t length) {
    const uint8_t* byte_data = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; ++i) {
        uint8_t table_index = (crc ^ byte_data[i]) & 0xFF;
        crc = (crc >> 8) ^ crctab[table_index];
    }

    return crc ^ 0xFFFFFFFF;
}

uint8_t calculate_crc(const Frame &frame) {
	uint8_t buffer[sizeof(frame) - sizeof(frame.crc)];
	std::memcpy(buffer, &frame, sizeof(buffer));
	
	return calculate_crc32(buffer, sizeof(buffer));
}

bool send_frame_and_receive_ack(int sockfd, Frame &frame, int timeout_seconds) {
	auto start = chrono::steady_clock::now();
	uint8_t buffer[sizeof(Frame)] = {0};

	safe_send(sockfd, reinterpret_cast<uint8_t *>(&frame), sizeof(Frame));
	cout << "Sent frame " << (int)frame.sequence << " (" << translate_frame_type(frame.type) << ")"
		 << endl;

	while (true) {
		cout << "Waiting for ack" << endl;
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
					cout << "Received frame " << (int)response->sequence << " ("
						 << translate_frame_type(response->type) << ")" << endl;
					if (response->type == TYPE_ACK) {
						return true;
					} 
				} else {
						cout << "CRC check failed" << endl;
				}
			} else {
				cout << "Wrong ack sequence received " << endl;
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
					cout << "Received frame " << (int)response->sequence << " ("
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

	send(sockfd, reinterpret_cast<void *>(&ack), sizeof(ack), 0);

	//cout << "Sent frame " << (int)sequence << " (" << translate_frame_type(ack.type) << ")" << endl;
}

void send_nack(int sockfd, uint8_t sequence) {
	Frame nack;
	nack.start_marker = START_MARKER;
	nack.length = 0;
	nack.sequence = sequence;
	nack.type = TYPE_NACK;
	nack.crc = calculate_crc(nack);

	send(sockfd, (void *)&nack, sizeof(nack), 0);
	cout << "Sent frame " << (int)sequence << " (" << translate_frame_type(nack.type) << ")"
		 << endl;
}

bool receive_ack(int sockfd, uint8_t &ack_sequence, int timeout_seconds) {
	Frame frame;
	if (receive_frame_with_timeout(sockfd, frame, timeout_seconds)) {
		if (frame.type == TYPE_ACK) {
			ack_sequence = frame.sequence;
			return true;
		} else {
			cout << "Wrong ack squence received" << endl;
		}
	}
	return false;
}


bool wait_for_response(int sockfd, Frame &response, int timeout_seconds) {
    fd_set readfds;
    struct timeval tv;
    
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;
    
		while (true) {
			int result = select(sockfd + 1, &readfds, NULL, NULL, &tv);
			if (result > 0) {
					recv(sockfd, &response, sizeof(Frame), 0);
					if (response.start_marker == START_MARKER) return true;
			} else {
					return false;
			}
		}
}

void send_window(int sockfd, vector<Frame> window) {
	cout << "Sending frames " << (int)window[0].sequence << " to " << (int)window[WINDOW_SIZE - 1].sequence << endl;
	for (int i = 0; i < window.size(); i++) {
		// cout << (int)window[i].sequence << " ";
		send(sockfd, &window[i], sizeof(window[i]), 0);
		if (window[i].type == TYPE_END_TX) {
			break;
		}
	}
	// cout << endl;
}

void send_file(int sockfd, ifstream &file, int timeout_seconds) {
	vector<Frame> window(WINDOW_SIZE);
	uint8_t seq_num = 255;
	uint8_t end_tx_seq_num;
	int retries = 0;
	bool sent_end_tx = false;

	int window_frame_index = 0;

	while (!file.eof() || !sent_end_tx) {
		// assemble window
		while (window_frame_index < WINDOW_SIZE && !file.eof()) {
			seq_num = (seq_num + 1) % MAX_SEQ;
			Frame &frame = window[window_frame_index];
			frame.start_marker = START_MARKER;
			frame.sequence = seq_num;
			frame.type = TYPE_DATA;
			file.read((char *)frame.data, sizeof(frame.data));
			frame.length = file.gcount();
			frame.crc = calculate_crc(frame);
			window_frame_index++;
		}

		// send end of transmition frame
		if (file.eof() && window_frame_index < WINDOW_SIZE) {
			seq_num = (seq_num + 1) % MAX_SEQ;
			Frame end_frame;
			end_frame.start_marker = START_MARKER;
			end_frame.sequence = seq_num;
			end_frame.type = TYPE_END_TX;
			end_frame.length = 0;
			end_frame.crc = calculate_crc(end_frame);

			window[window_frame_index] = end_frame;
			sent_end_tx = true;
			end_tx_seq_num = seq_num;
		}

		send_window(sockfd, window);


		Frame response;
		bool response_received = wait_for_response(sockfd, response, timeout_seconds);

		if(!response_received) {
			cout << "Client timed out, resending window" << endl;
			retries++;
			if (retries > MAX_RETIES) {
				cout << "Max retries reached. Terminating connection" << endl;
				file.close();
				return;
			}
			continue;
		} else {
			retries = 0;
		}

		if (response.type == TYPE_NACK) {
			cout << "Received " << translate_frame_type(response.type) << " " << (int)response.sequence << endl;
			// move window
			uint8_t nack_seq = response.sequence;
			uint8_t nack_index;
			for (int i = 0; i < window.size(); i++) {
				if (window[i].sequence == nack_seq) {
					nack_index = i;
					break;
				}
			}

			vector<Frame> new_window(WINDOW_SIZE);
			for (int i = nack_index, j = 0; i < window.size(); i++, j++) {
				new_window[j] = window[i];
				window_frame_index = j + 1;
			}
			window = new_window;
		} else if (response.type == TYPE_ACK && sent_end_tx && response.sequence == end_tx_seq_num) {
			cout << "Client acknoledged end of transmition" << endl;
		} else if (response.type == TYPE_ACK && response.sequence == seq_num) {
			// cout << "Got ack for whole window" << endl;
			window_frame_index = 0;
		}
	}
}
