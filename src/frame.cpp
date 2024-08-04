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


// Send a frame until gets ack
void send_frame_and_receive_ack(int sockfd, Frame &frame, int timeout_seconds) {
	send(sockfd, (void*) &frame, sizeof(frame), 0);

	Frame response;
	while (true) {
		bool received = receive_frame_with_timeout(sockfd, response, timeout_seconds);

		if (!received || (response.type == TYPE_NACK && response.sequence == frame.sequence)) {
			send(sockfd, (void*) &frame, sizeof(frame), 0);
			if (SHOW_LOGS == 1) cout << "Timed out, resending frame " << (int)frame.sequence << " (" << translate_frame_type(frame.type) << ")"
		 	<< endl;
		} else if (response.type == TYPE_ACK && response.sequence == frame.sequence) {
			return;
		}
	}
}


// used for receiving acks and nacks
bool receive_frame_with_timeout(int sockfd, Frame &frame, int timeout_seconds) {
	auto start = chrono::steady_clock::now();

	while (true) {
		ssize_t len = recv(sockfd, (void*) &frame, sizeof(Frame), 0);

		if (len > 0) {
			// Received a package, check if it is what was expected
			if (frame.start_marker == START_MARKER) {
				return true;
			}
		}

		auto now = chrono::steady_clock::now();
		auto elapsed_seconds = chrono::duration_cast<chrono::seconds>(now - start).count();
		// Timout
		if (elapsed_seconds >= timeout_seconds) {
			return false;
		}
	}
}


// receive a frame until its right and send ack
void receive_frame_and_send_ack(int sockfd, uint8_t seq, Frame &frame) {
	random_device rd;
	mt19937 gen(rd());
	uniform_int_distribution<> dist(1, ERRORS_FREQ_LIST);

	while (true) {
		ssize_t bytes_received = recv(sockfd, (void *) &frame, sizeof(Frame), 0);
			
		if (bytes_received < 0 || frame.start_marker != START_MARKER) {
			continue;
		}

		int rand = TEST_ERRORS == 1 ? dist(gen) : -1;

		if (frame.crc != calculate_crc(frame) || frame.sequence != seq || rand == 1) {
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
}

void send_nack(int sockfd, uint8_t sequence) {
	Frame nack;
	nack.start_marker = START_MARKER;
	nack.length = 0;
	nack.sequence = sequence;
	nack.type = TYPE_NACK;
	nack.crc = calculate_crc(nack);

	send(sockfd, (void *)&nack, sizeof(nack), 0);
	if (SHOW_LOGS == 1) cout << "Sent NACK to " << (int)sequence << endl;
}