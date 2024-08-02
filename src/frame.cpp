#include "../inc/frame.h"

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

uint32_t calculate_crc8(const uint8_t* buf, size_t size) {
    uint8_t crc8 = 0;

    for (size_t i = 0; i < size; i++) {
        crc8 = crc8_table[crc8 ^ buf[i]];
    }

    return crc8;
}

uint8_t calculate_crc(const Frame &frame) {
	uint8_t buffer[sizeof(frame) - sizeof(frame.crc)];
	std::memcpy(buffer, &frame, sizeof(buffer));
	
	return calculate_crc8(buffer, sizeof(buffer));
}

// SENDER
// Send a frame until gets ack
void send_frame_and_receive_ack(int sockfd, Frame &frame, int timeout_seconds) {
	send(sockfd, (void*) &frame, sizeof(frame), 0);
	cout << "Sent frame " << (int)frame.sequence << " (" << translate_frame_type(frame.type) << ")"
		 << endl;

	Frame response;
	while (true) {
		bool received = receive_frame_with_timeout(sockfd, response, timeout_seconds);

		if (!received || (response.type == TYPE_NACK && response.sequence == frame.sequence)) {
			send(sockfd, (void*) &frame, sizeof(frame), 0);
			cout << "Resent frame " << (int)frame.sequence << " (" << translate_frame_type(frame.type) << ")"
		 	<< endl;
		} else if (response.type == TYPE_ACK && response.sequence == frame.sequence) {
			return;
		}
	}
}

// SENDER
// used for receiving acks and nacks
bool receive_frame_with_timeout(int sockfd, Frame &frame, int timeout_seconds) {
	auto start = chrono::steady_clock::now();

	while (true) {
		ssize_t len = recv(sockfd, (void*) &frame, sizeof(Frame), 0);

		if (len > 0) {
			// Received a package, check if it is what was expected
			if (frame.start_marker == START_MARKER) {
				// cout << "Received frame " << (int)response->sequence << " ("
				// 		<< translate_frame_type(response->type) << ")" << endl;
				return true;
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

// RECEIVER
// receive a frame until its right and send ack
void receive_frame_and_send_ack(int sockfd, uint8_t seq, Frame &frame) {
	while (true) {
		ssize_t bytes_received = recv(sockfd, (void *) &frame, sizeof(Frame), 0);
			
		if (bytes_received < 0 || frame.start_marker != START_MARKER) {
			continue;
		}

		if (frame.crc != calculate_crc(frame) || frame.sequence != seq) {
			send_nack(sockfd, seq);
		} else {
			send_ack(sockfd, seq);
			break;
		}
	}
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

void send_window(int sockfd, vector<Frame> window) {
	cout << "Sending frames " << (int)window[0].sequence << " to " << (int)window[WINDOW_SIZE - 1].sequence << endl;
	for (size_t i = 0; i < window.size(); i++) {
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
		// bool response_received = wait_for_response(sockfd, response, timeout_seconds);

		bool response_received = receive_frame_with_timeout(sockfd, response, timeout_seconds);

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
			for (size_t i = 0; i < window.size(); i++) {
				if (window[i].sequence == nack_seq) {
					nack_index = i;
					break;
				}
			}

			vector<Frame> new_window(WINDOW_SIZE);
			for (size_t i = nack_index, j = 0; i < window.size(); i++, j++) {
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
