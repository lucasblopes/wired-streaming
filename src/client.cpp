#include "../inc/client.h"

using namespace std;

int calculateIndex(int baseSeqNum, int seqNum) {
		int seq = baseSeqNum;
    for (int i = 0;;i++) {
			if (seq == seqNum) return i;
			seq = (seq+1) % MAX_SEQ;
		}
}

void receive_bugged_window(int sockfd, ofstream &file, uint8_t first_seq) {
	vector<Frame> window(WINDOW_SIZE);
	vector<int> hasReceived(WINDOW_SIZE, 0);
	Frame frame;
	int received = 0;
	int end_tx_index = INT8_MAX;

	if (SHOW_LOGS == 1) cout << "Receiving bugged window starting from frame " << (int)first_seq << endl;
	while (received < WINDOW_SIZE) {
		ssize_t bytes_received = recv(sockfd, (void*) &frame, sizeof(Frame), 0); 
			if (bytes_received < 0 || frame.start_marker != START_MARKER) {
				continue;
			} else if (frame.type == TYPE_END_TX) {
				end_tx_index = (calculateIndex(first_seq, (int)frame.sequence));
				break;
			}
			window[calculateIndex(first_seq, (int)frame.sequence)] = frame;
			hasReceived[calculateIndex(first_seq, (int)frame.sequence)] = 1;
			received++;
	}

	for (int i = 0; i < WINDOW_SIZE && i < end_tx_index; i++) {
		if (hasReceived[i] == 0) {
			send_nack(sockfd, first_seq);
			return receive_bugged_window(sockfd, file, first_seq);
		}
	}

	for (int i = 0; i < WINDOW_SIZE && i < end_tx_index; i++) {
		Frame f = window[i];
		if (SHOW_LOGS == 1) cout << "Got frame " << (int)f.sequence << endl;
		file.write((char *)f.data, f.length);
	}

	if (end_tx_index < INT8_MAX) {
		send_ack(sockfd, window[end_tx_index].sequence);
		cout << "File downloaded" << endl;
		exit(0);
	}
	send_ack(sockfd, window[WINDOW_SIZE - 1].sequence);

	return;
}

bool receive_file(int sockfd, ofstream &file) {
	uint8_t first_window_seq = 0, last_window_seq = WINDOW_SIZE - 1, expected_sequence = 0;
	unordered_set<uint8_t> window_frames_written;
	int nacks_sent = 0;
	cout << "Receiving file..." << endl;

	random_device rd;
	mt19937 gen(rd());
	uniform_int_distribution<> dist(1, ERRORS_FREQ_DOWNLOAD);

	while(true) {
		Frame frame;
		ssize_t bytes_received = recv(sockfd, static_cast<void *>(&frame), sizeof(Frame), 0);
		if (bytes_received < 0 || frame.start_marker != START_MARKER) {
			continue;
		}

		// Got error
		if (frame.type == TYPE_ERROR) {
			send_ack(sockfd, frame.sequence);
			return false;
		}

		int rand = TEST_ERRORS == 1 ? dist(gen) : -1;

		// Something went wrong, send nack
		if (frame.crc != calculate_crc(frame) || frame.sequence != expected_sequence || rand == 1) {
			// jogar fora o resto da janela
			uint8_t received = window_frames_written.size() + 1;
			while (received < WINDOW_SIZE) {
				recv(sockfd, static_cast<void *>(&frame), sizeof(Frame), 0);
				if (frame.start_marker == START_MARKER) {
					if (frame.type == TYPE_END_TX) {
						break;
					}
					received++;
				}
			}

			// window is 'bugged'
			if (nacks_sent > 2 && FIX_BUGGED_WINDOWS == 1) {
				send_nack(sockfd, expected_sequence);
				receive_bugged_window(sockfd, file, expected_sequence);
				window_frames_written.clear();
				first_window_seq = (expected_sequence + WINDOW_SIZE) % MAX_SEQ;
				last_window_seq = (first_window_seq + WINDOW_SIZE - 1) % MAX_SEQ;
				expected_sequence = first_window_seq;
				nacks_sent = 0;
				continue;
			}

			// send nack and update window
			window_frames_written.clear();
			first_window_seq = expected_sequence;
			last_window_seq = (first_window_seq + WINDOW_SIZE - 1) % MAX_SEQ;
			nacks_sent++;
			send_nack(sockfd, expected_sequence);
		} else if (frame.type == TYPE_END_TX) {
			send_ack(sockfd, frame.sequence);
			return true;
		} else {
			nacks_sent = 0;
			if (window_frames_written.find(frame.sequence) == window_frames_written.end()) {
				window_frames_written.insert(frame.sequence);
				if (SHOW_LOGS == 1) cout << "Got frame " << (int)frame.sequence << endl;
				file.write((char *)frame.data, frame.length);
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
			return file_list;
		} else if (frame.type == TYPE_ERROR) {
			cout << "Server failed to send file list" << endl;
			return {};
		}
		file_list.push_back((char *)frame.data);
		next_seq_num = (next_seq_num + 1) % MAX_SEQ;
	}
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
		cout << "Server failed to send file" << endl;
	} else {
		file.close();
		cout << "File " << filename << " downloaded successfully" << endl;
	}
	
}