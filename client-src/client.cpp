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
#include "../inc/raw-socket.h"

using namespace std;

void receive_file(int sockfd, ofstream &file, int timeout_seconds) {
	vector<Frame> window(WINDOW_SIZE);
	uint8_t expected_sequence = 0;
	cout << "Receiving file..." << endl;

	while (true) {
		Frame frame;
		if (receive_frame_and_send_ack(sockfd, frame, timeout_seconds)) {
			if (frame.sequence == expected_sequence) {
				if (frame.type == TYPE_END_TX) {
					cout << "Received end of transmission frame." << endl;
					break;
				} else if (frame.type == TYPE_DATA) {
					file.write((char *)frame.data, frame.length);
				} else {
					send_nack(sockfd, expected_sequence);
				}
				expected_sequence = (expected_sequence + 1) % WINDOW_SIZE;
			}
		}
	}
}

vector<string> list_files(int sockfd, int timeout_seconds) {
	Frame list_request = {};
	list_request.start_marker = START_MARKER;
	list_request.length = 0;
	list_request.sequence = 0;
	list_request.type = TYPE_LIST;
	list_request.crc = calculate_crc(list_request);

	vector<string> file_list;

	if (send_frame_and_receive_ack(sockfd, list_request, timeout_seconds)) {
		uint8_t next_seq_num = 0;
		while (true) {
			Frame frame = {};
			if (receive_frame_and_send_ack(sockfd, frame, timeout_seconds)) {
				if (frame.type == TYPE_END_TX) {
					break;
				}
				if (frame.sequence == next_seq_num) {
					file_list.push_back((char *)frame.data);
					next_seq_num = (next_seq_num + 1) % WINDOW_SIZE;
				}
			}
		}
	} else {
		cout << "Failed to request file list" << endl;
	}

	return file_list;
}

void download_file(int sockfd, const string &filename, int timeout_seconds) {
	Frame frame;
	frame.start_marker = START_MARKER;
	frame.length = filename.size();
	frame.sequence = 0;
	frame.type = TYPE_DOWNLOAD;
	strncpy((char *)frame.data, filename.c_str(), frame.length);
	frame.crc = calculate_crc(frame);

	if (send_frame_and_receive_ack(sockfd, frame, timeout_seconds)) {
		ofstream file(filename, ios::binary);
		if (!file.is_open()) {
			cout << "Failed to open " << filename << endl;
			return;
		}

		receive_file(sockfd, file, timeout_seconds);
		file.close();
		cout << "File " << filename << " downloaded successfully" << endl;
	} else {
		cout << "Failed to request download for " << filename << endl;
	}
}

int main() {
	const char *interface_name = INTERFACE_NAME;
	int timeout_seconds = TIMEOUT_SECONDS;
	int sockfd = raw_socket_create(interface_name, timeout_seconds);

	cout << "Client started. Listing available files..." << endl;
	vector<string> file_list = list_files(sockfd, timeout_seconds);

	if (!file_list.empty()) {
		int choice;
		while (true) {
			cout << "Enter the number of the file you want to download: " << endl;
			for (size_t i = 0; i < file_list.size(); ++i) {
				cout << i + 1 << ": " << file_list[i] << endl;
			}
			cin >> choice;
			if (choice > 0 && choice <= (int)file_list.size()) {
				break;
			} else {
				cout << "Invalid choice. Try again:" << endl;
			}
		}
		cout << file_list[choice - 1] << endl;
		download_file(sockfd, file_list[choice - 1], timeout_seconds);
	} else {
		cout << "No files available for download" << endl;
	}

	close(sockfd);
	return 0;
}
