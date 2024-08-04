#include "../inc/client.h"

/*===== CLIENT ====*/
int main() {
	const char *interface_name = INTERFACE_NAME;
	int timeout_seconds = TIMEOUT_SECONDS;
	int sockfd = raw_socket_create(interface_name, timeout_seconds);

	cout << "Client started. Sending list request..." << endl;
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
