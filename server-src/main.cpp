#include "../inc/server.h"

using namespace std;

/*===== SERVER =====*/
int main() {
	const char *interface_name = INTERFACE_NAME;
	int timeout_seconds = TIMEOUT_SECONDS;
	int sockfd = raw_socket_create(interface_name, timeout_seconds);

	cout << "Server started" << endl;

	Frame request;

	while (true) {
		listen_for_requests(sockfd, request);
		switch (request.type) {
			case TYPE_LIST:
				send_ack(sockfd, request.sequence);
				cout << "Got list request" << endl;
				handle_list_request(sockfd, timeout_seconds);
				break;
			case TYPE_DOWNLOAD:
				send_ack(sockfd, request.sequence);
				cout << "Got download request" << endl;
				handle_download_request(sockfd, request, timeout_seconds);
				break;
			default:
				if (SHOW_LOGS == 1) cout << "Invalid request received" << endl;
				break;
		}
	}

	close(sockfd);
	return 0;
}
