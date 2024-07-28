#include <arpa/inet.h>
#include <dirent.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#include <random>
#include <unordered_set>
#include <chrono>
#include <thread>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "../inc/frame.h"
#include "../inc/raw-socket.h"

using namespace std;

bool receive_file(int sockfd, ofstream &file, int timeout_seconds) {
	uint8_t first_window_seq = 0, last_window_seq = WINDOW_SIZE - 1, expected_sequence = 0;
	unordered_set<uint8_t> window_frames_written;
	long int size = 0;
	cout << "Receiving file..." << endl;

	random_device rd;

	mt19937 gen(rd());

	uniform_int_distribution<> dist(1, 100);

	while(true) {
		Frame frame;
		size_t bytes_received = recv(sockfd, static_cast<void *>(&frame), sizeof(Frame), 0);
		if (bytes_received < 0 || frame.start_marker != START_MARKER) {
			continue;
		}

		// ERROR
		if (frame.type == TYPE_ERROR) {
			send_ack(sockfd, frame.sequence);
			return false;
		}

		int rand = dist(gen);
		int rand2 = dist(gen);
		if (rand2 == 3) {
			cout << "Sleeping for 10s" << endl;
			this_thread::sleep_for(chrono::seconds(10));
		}
		if (frame.crc != calculate_crc(frame) || rand == 1) {
			cout << "crc failed for " << (int)frame.sequence << endl;
			while (true) {
				recv(sockfd, static_cast<void *>(&frame), sizeof(Frame), 0);
				if (frame.sequence == last_window_seq || frame.type == TYPE_END_TX) {
					break;
				}
			}
			window_frames_written.clear();
			first_window_seq = expected_sequence;
			last_window_seq = (first_window_seq + WINDOW_SIZE - 1) % MAX_SEQ;
			send_nack(sockfd, expected_sequence);
		} else if (frame.sequence != expected_sequence  || rand == 2) {
			cout << "Wrong sequence, expected " << (int)expected_sequence << " got " << (int)frame.sequence << endl;
			while (true) {
				recv(sockfd, static_cast<void *>(&frame), sizeof(Frame), 0);
				if (frame.sequence == last_window_seq || frame.type == TYPE_END_TX) {
					break;
				}
			}
			expected_sequence = first_window_seq;
			send_nack(sockfd, first_window_seq);
		} else if (frame.type == TYPE_END_TX) {
			cout << "Received end of transmition frame" << endl;;
			send_ack(sockfd, frame.sequence);
			return true;
		} else {
			if (window_frames_written.find(frame.sequence) == window_frames_written.end()) {
				window_frames_written.insert(frame.sequence);
				file.write((char *)frame.data, frame.length);
				size+=frame.length;
			}

			if (frame.sequence == last_window_seq) {
				send_ack(sockfd, last_window_seq);
				window_frames_written.clear();
				first_window_seq = (expected_sequence + 1) % MAX_SEQ;
				last_window_seq = (first_window_seq + WINDOW_SIZE - 1) % MAX_SEQ;
			}
			expected_sequence = (expected_sequence + 1) % MAX_SEQ;
		}
	}
	return false;
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
					next_seq_num = (next_seq_num + 1) % MAX_SEQ;
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

		if (!receive_file(sockfd, file, timeout_seconds)) {
			file.close();
			remove((char*)&filename);
			cout << "Could not download file" << endl;
		} else {
			file.close();
			cout << "File " << filename << " downloaded successfully" << endl;
		}
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
