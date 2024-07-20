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

void handle_list_request(int sockfd, int timeout_seconds) {
	const string directory_path = "./videos";
	vector<string> files;
	struct dirent *entry;
	DIR *dp = opendir(directory_path.c_str());

	if (dp == nullptr) {
		perror("opendir");
		return;
	}

	while ((entry = readdir(dp))) {
		if (entry->d_type == DT_REG) {	// Regular file
			files.emplace_back(entry->d_name);
		}
	}
	closedir(dp);

	uint8_t video_sequence = 0;
	for (const auto &file : files) {
		Frame frame = {};
		frame.start_marker = START_MARKER;
		frame.length = file.size();
		frame.sequence = video_sequence;
		frame.type = TYPE_FILL_DESCRIPTOR;
		strncpy((char *)frame.data, file.c_str(), frame.length);
		frame.crc = calculate_crc(frame);

		if (!send_frame_and_receive_ack(sockfd, frame, timeout_seconds)) {
			cout << "Failed to send file list" << endl;
			exit(1);
		}
		video_sequence = (video_sequence + 1) % WINDOW_SIZE;
	}

	Frame end_tx_frame = {};
	end_tx_frame.start_marker = START_MARKER;
	end_tx_frame.length = 0;
	end_tx_frame.sequence = 0;
	end_tx_frame.type = TYPE_END_TX;
	end_tx_frame.crc = calculate_crc(end_tx_frame);

	send_frame_and_receive_ack(sockfd, end_tx_frame, timeout_seconds);
}

void handle_download_request(int sockfd, const Frame &frame, int timeout_seconds) {
	string filename((char *)frame.data, frame.length);
	ifstream file("./videos/" + filename, ios::binary);
	cout << "./videos/" << filename << endl;

	if (file.is_open()) {
		send_file(sockfd, file, timeout_seconds);
		file.close();
	} else {
		cout << "Failed to open file: " << filename << endl;
		Frame error_frame;
		error_frame.start_marker = START_MARKER;
		error_frame.length = 0;
		error_frame.sequence = 0;
		error_frame.type = TYPE_ERROR;
		error_frame.crc = calculate_crc(error_frame);

		send_frame_and_receive_ack(sockfd, error_frame, timeout_seconds);
	}
}

int main() {
	const char *interface_name = INTERFACE_NAME;
	int timeout_seconds = TIMEOUT_SECONDS;
	int sockfd = raw_socket_create(interface_name, timeout_seconds);

	cout << "Server started, waiting for requests..." << endl;

	Frame frame;

	while (true) {
		if (receive_frame_and_send_ack(sockfd, frame, timeout_seconds)) {
			switch (frame.type) {
				case TYPE_LIST:
					handle_list_request(sockfd, timeout_seconds);
					break;
				case TYPE_DOWNLOAD:
					handle_download_request(sockfd, frame, timeout_seconds);
					break;
				default:
					cout << "Unknown frame type received" << endl;
					break;
			}
		}
	}

	close(sockfd);
	return 0;
}
