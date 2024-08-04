#include "../inc/server.h"

using namespace std;

void handle_list_request(int sockfd, int timeout_seconds) {
	const string directory_path = "./videos";
	vector<string> files;
	struct dirent *entry;
	DIR *dp = opendir(directory_path.c_str());

	if (dp == nullptr) {
		Frame frame = {};
		frame.start_marker = START_MARKER;
		frame.length = 0;
		frame.sequence = 0;
		frame.type = TYPE_ERROR;
		frame.crc = calculate_crc(frame);
		send_frame_and_receive_ack(sockfd, frame, timeout_seconds);
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

void send_window(int sockfd, vector<Frame> window) {
	if (SHOW_LOGS == 1) cout << "Sending frames " << (int)window[0].sequence << " to " << (int)window[WINDOW_SIZE - 1].sequence << endl;
	for (size_t i = 0; i < window.size(); i++) {
		send(sockfd, &window[i], sizeof(window[i]), 0);
		if (window[i].type == TYPE_END_TX) {
			break;
		}
	}
}

void send_file(int sockfd, ifstream &file, int timeout_seconds) {
	vector<Frame> window(WINDOW_SIZE);
	uint8_t seq_num = 255;
	int retries = 0;
	bool sent_end_tx = false;
	bool got_end_ack = false;

	int window_frame_index = 0;

	while (!file.eof() || !sent_end_tx || !got_end_ack) {
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

		// put end of transmition frame on window
		if (file.eof() && window_frame_index < WINDOW_SIZE && !sent_end_tx) {
			seq_num = (seq_num + 1) % MAX_SEQ;
			Frame end_frame;
			end_frame.start_marker = START_MARKER;
			end_frame.sequence = seq_num;
			end_frame.type = TYPE_END_TX;
			end_frame.length = 0;
			end_frame.crc = calculate_crc(end_frame);

			window[window_frame_index] = end_frame;
			sent_end_tx = true;
		}

		send_window(sockfd, window);


		Frame response;
		bool response_received = receive_frame_with_timeout(sockfd, response, timeout_seconds);

		if(!response_received) {
			if (SHOW_LOGS == 1) cout << "Timed out, resending window" << endl;
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
			// move window
			uint8_t nack_seq = response.sequence;
			uint8_t nack_index = INT8_MAX;
			for (size_t i = 0; i < window.size(); i++) {
				if (window[i].sequence == nack_seq) {
					nack_index = i;
					break;
				}
			}

			vector<Frame> new_window(WINDOW_SIZE);
			window_frame_index = 0;
			for (size_t i = nack_index, j = 0; i < window.size(); i++, j++) {
				new_window[j] = window[i];
				window_frame_index = j + 1;
			}
			window = new_window;
		} else if (response.type == TYPE_ACK && response.sequence == seq_num) {
			window_frame_index = 0;
			if (sent_end_tx) {
				got_end_ack = true;
			}
			cout << "got ack" << endl;
		}
	}

}

void handle_download_request(int sockfd, const Frame &frame, int timeout_seconds) {
	string filename((char *)frame.data, frame.length);
	ifstream file("./videos/" + filename, ios::binary);
	cout << "Sending " << "./videos/" << filename << endl;

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
	cout << "Listening for requests..." << endl;
	while (true) {
		ssize_t bytes_received = recv(sockfd, (void*) &request, sizeof(Frame), 0);
		if (bytes_received > 0 && request.start_marker == START_MARKER) {
			return;
		}
	}
}