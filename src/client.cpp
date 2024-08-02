#include "../inc/client.h"

using namespace std;

bool receive_file(int sockfd, ofstream &file) {
	uint8_t first_window_seq = 0, last_window_seq = WINDOW_SIZE - 1, expected_sequence = 0;
	unordered_set<uint8_t> window_frames_written;
	long int size = 0;
	cout << "Receiving file..." << endl;

	random_device rd;

	mt19937 gen(rd());

	uniform_int_distribution<> dist(1, 100);

	while(true) {
		Frame frame;
		ssize_t bytes_received = recv(sockfd, static_cast<void *>(&frame), sizeof(Frame), 0);
		if (bytes_received < 0ll || frame.start_marker != START_MARKER) {
			continue;
		}

		// ERROR
		if (frame.type == TYPE_ERROR) {
			send_ack(sockfd, frame.sequence);
			return false;
		}

		int rand = dist(gen);
		rand = -1;

		if (frame.crc != calculate_crc(frame) || frame.sequence != expected_sequence || rand == 1) {
			// jogar fora o resto da janela
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

	send_frame_and_receive_ack(sockfd, list_request, timeout_seconds);
	uint8_t next_seq_num = 0;
	while (true) {
		Frame frame = {};
		receive_frame_and_send_ack(sockfd, next_seq_num, frame);
		if (frame.type == TYPE_END_TX) {
			break;
		}
		file_list.push_back((char *)frame.data);
		next_seq_num = (next_seq_num + 1) % MAX_SEQ;
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

	ofstream file(filename, ios::binary);
	if (!file.is_open()) {
		cout << "Failed to create file " << filename << endl;
		return;
	}

	send_frame_and_receive_ack(sockfd, frame, timeout_seconds);
	
	if (!receive_file(sockfd, file)) {
		file.close();
		remove((char*)&filename);
		cout << "Could not download file" << endl;
	} else {
		file.close();
		cout << "File " << filename << " downloaded successfully" << endl;
	}
	
}