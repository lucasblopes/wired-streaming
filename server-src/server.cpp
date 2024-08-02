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

	uint8_t seq = 0;
	for (const auto &file : files) {
		Frame frame = {};
		frame.start_marker = START_MARKER;
		frame.length = file.size();
		frame.sequence = seq;
		frame.type = TYPE_FILE_DESCRIPTOR;
		strncpy((char *)frame.data, file.c_str(), frame.length);
		frame.crc = calculate_crc(frame);

		send_frame_and_receive_ack(sockfd, frame, timeout_seconds);
		seq = (seq + 1) % MAX_SEQ;
	}

	Frame end_tx_frame = {};
	end_tx_frame.start_marker = START_MARKER;
	end_tx_frame.length = 0;
	end_tx_frame.sequence = seq;
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

void listen_for_requests(int sockfd, Frame &request) {
	while (true) {
		size_t bytes_received = recv(sockfd, (void*) &request, sizeof(Frame), 0);
		if (bytes_received > 0 && request.start_marker == START_MARKER) {
			return;
		}
	}
}

int main() {
	const char *interface_name = INTERFACE_NAME;
	int timeout_seconds = TIMEOUT_SECONDS;
	int sockfd = raw_socket_create(interface_name, timeout_seconds);

	cout << "Server started, waiting for requests..." << endl;

	Frame request;

	while (true) {
		listen_for_requests(sockfd, request);
		switch (request.type) {
			case TYPE_LIST:
				send_ack(sockfd, request.sequence);
				handle_list_request(sockfd, timeout_seconds);
				break;
			case TYPE_DOWNLOAD:
				send_ack(sockfd, request.sequence);
				handle_download_request(sockfd, request, timeout_seconds);
				break;
			default:
				cout << "Invalid request received" << endl;
				break;
		}
	}

	close(sockfd);
	return 0;
}
