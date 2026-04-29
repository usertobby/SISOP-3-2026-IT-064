#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "protocol.h"

// Main Function
int main() {
    int sock;
    struct sockaddr_in serv_addr;
    fd_set fds;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    char name[50];
	char buffer[BUFFER_SIZE];
	int len;
	int is_admin = 0;

	// Login Loop
	while(1) {
    	printf("Enter your name: ");
		fflush(stdout);

    	fgets(name, sizeof(name), stdin);
    	name[strcspn(name, "\n")] = 0;

    	send(sock, name, strlen(name), 0);

    	len = recv(sock, buffer, sizeof(buffer)-1, 0);
		if(len <= 0) {
			printf("Connection closed.\n");
			return 0;
		}

    	buffer[len] = '\0';
    	printf("%s", buffer);
		fflush(stdout);

		// Duplicate Name
		if(strstr(buffer, "already synchronized")) {
			close(sock);
			return 0;
		}

    	// Handle Password Admin
    	if(strstr(buffer, "Enter Password")) {
			char pass[50];

			fgets(pass, sizeof(pass), stdin);
			send(sock, pass, strlen(pass), 0);

			len = recv(sock, buffer, sizeof(buffer)-1, 0);
			if(len <= 0) {
				printf("Connection closed.\n");
				return 0;
			}

			buffer[len] = '\0';
			printf("%s", buffer);
			fflush(stdout);

			if(strstr(buffer, "Authentication Failed")) {
				close(sock);
				return 0;
			}

			// If Admin
			if(strstr(buffer, "Authentication Successful")) {
				is_admin = 1;

				printf("=== THE KNIGHTS CONSOLE ===\n");
				printf("1. Check Active Entities (Users)\n");
				printf("2. Check Server Uptime\n");
				printf("3. Execute Emergency Shutdown\n");
				printf("4. Disconnect\n\n");
			}
    	}

		break;
	}

    // Loop
	if(is_admin) {
	    printf("Command >> ");
	} else {
	    printf("> ");
	}
	fflush(stdout);

	int waiting_response = 0;	// flag

    while (1) {
		FD_ZERO(&fds);
		FD_SET(0, &fds);	// stdin
		FD_SET(sock, &fds);	// server

		select(sock+1, &fds, NULL, NULL, NULL);

		// User Input
		if(FD_ISSET(0, &fds)) {
	    	char msg[BUFFER_SIZE];

	    	fgets(msg, sizeof(msg), stdin);

	    	// Disconnect
	    	if(strcmp(msg, "/exit\n") == 0 || (is_admin && strncmp(msg, "4", 1) == 0)) {
				send(sock, msg, strlen(msg), 0);
				printf("[System] Disconnecting from The Wired...\n");
				close(sock);
				break;
	    	}

			send(sock, msg, strlen(msg), 0);
			waiting_response = 1;
		}

		// Server Messages
        if(FD_ISSET(sock, &fds)) {
            int len = recv(sock, buffer, sizeof(buffer)-1, 0);

            if(len <= 0) {
                printf("[System] Disconnecting from The Wired...\n");
                break;
            }

            buffer[len] = '\0';
            printf("%s", buffer);

			waiting_response = 0;

			if(is_admin) {
	    		printf("Command >> ");
			} else {
	    		printf("> ");
			}
			fflush(stdout);
        }
    }

    return 0;
}
