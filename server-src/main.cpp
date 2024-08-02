#include "../inc/server.h"

using namespace std;

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
