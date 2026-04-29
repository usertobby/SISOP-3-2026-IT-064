#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <time.h>

#include "protocol.h"

typedef struct {
    int sock;
    char name[50];
    int is_admin;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;

// Logging Function
void log_event(const char *role, const char *msg) {
    FILE *f = fopen("history.log", "a");

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
	tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	tm->tm_hour, tm->tm_min, tm->tm_sec,
	role, msg);

    fclose(f);
}

// User Chats Logging Function
void log_chat(const char *user, const char *msg) {
    FILE *f = fopen("history.log", "a");

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

	char clean_msg[BUFFER_SIZE];
    strncpy(clean_msg, msg, sizeof(clean_msg)-1);
    clean_msg[sizeof(clean_msg)-1] = '\0';
    clean_msg[strcspn(clean_msg, "\n")] = '\0';

    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [User] [[%s]: %s]\n",
	tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	tm->tm_hour, tm->tm_min, tm->tm_sec,
	user, clean_msg);

    fclose(f);
}

// Check Name Exist Function
int is_name_exist(const char *name) {
    for(int i = 0; i < client_count; i++) {
		if(strcmp(clients[i].name, name) == 0) {
	    	return 1;
		}
    }
    return 0;
}

// Remove Client Function
void remove_client(int index) {
    char logbuf[128];
    sprintf(logbuf, "[User '%s' disconnected]", clients[index].name);
    log_event("System", logbuf);

    close(clients[index].sock);

    for(int i = index; i < client_count - 1; i++) {
		clients[i] = clients[i+1];
    }
    client_count--;
}

// Broadcast Function
void broadcast(const char *msg, int sender_sock) {
    for(int i = 0; i < client_count; i++) {
		if(clients[i].sock != sender_sock && clients[i].is_admin == 0) {
	    	send(clients[i].sock, msg, strlen(msg), 0);
		}
    }
}

time_t server_start_time;

// Main Function
int main() {
    int server_fd, new_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    fd_set readfds;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);

    log_event("System", "[SERVER ONLINE]");
    printf("Server running on port %d...\n", PORT);

    server_start_time = time(NULL);

    while(1) {
		FD_ZERO(&readfds);
		FD_SET(server_fd, &readfds);
		int max_fd = server_fd;

		for(int i = 0; i < client_count; i++) {
	    	FD_SET(clients[i].sock, &readfds);
	    	if(clients[i].sock > max_fd) {
				max_fd = clients[i].sock;
	    	}
		}

		select(max_fd + 1, &readfds, NULL, NULL, NULL);

		// New Client
		if(FD_ISSET(server_fd, &readfds)) {
	    	new_sock = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);

	    	char name[50];
	    	memset(name, 0, sizeof(name));
	    	recv(new_sock, name, sizeof(name), 0);

	    	name[strcspn(name, "\n")] = 0;	// remove newline

	    	// Admin Credentials
	    	if(strcmp(name, "The Knights") == 0) {
				char password[50];

				send(new_sock, "Enter Password: ", 16, 0);
				recv(new_sock, password, sizeof(password), 0);
				password[strcspn(password, "\n")] = 0;

				// Password Check
				if(strcmp(password, "protocol7") != 0) {
					send(new_sock, "[System] Authentication Failed.\n", 32, 0);
					close(new_sock);
					continue;
				}

				clients[client_count].sock = new_sock;
				strcpy(clients[client_count].name, name);
				clients[client_count].is_admin = 1;
				client_count++;

				char logbuf[128];
                sprintf(logbuf, "[User '%s' connected]", name);
                log_event("System", logbuf);

				send(new_sock, "[System] Authentication Successful. Granted Admin Privileges.\n\n", 67, 0);

				continue;
	    	}

	    	// Normal User
	    	if(is_name_exist(name)) {
				char msg[128];
				sprintf(msg, "[System] The identity '%s' is already synchronized in The Wired.\n", name);
				send(new_sock, msg, strlen(msg), 0);
				close(new_sock);
	    	} else {
				clients[client_count].sock = new_sock;
				strcpy(clients[client_count].name, name);
				clients[client_count].is_admin = 0;
				client_count++;

				char logbuf[128];
				sprintf(logbuf, "[User '%s' connected]", name);
				log_event("System", logbuf);

				char welcome[128];
				sprintf(welcome, "--- Welcome to The Wired, %s ---\n", name);
				send(new_sock, welcome, strlen(welcome), 0);
	    	}
		}

		// Handle Client
		for(int i = 0; i < client_count; i++) {
			if(FD_ISSET(clients[i].sock, &readfds)) {
				// Admin Handler
				if(clients[i].is_admin == 1) {
					char buffer[BUFFER_SIZE];
					int len = recv(clients[i].sock, buffer, sizeof(buffer), 0);

					if(len <= 0) {
						remove_client(i);
						i--;
						continue;
					}

					buffer[len] = '\0';

					// Option 1
					if(strncmp(buffer, "1", 1) == 0) {
						log_event("Admin", "[RPC_GET_USERS]");

						int count = 0;

						for(int j = 0; j < client_count; j++) {
							if(clients[j].is_admin == 0) {
								count++;
							}
						}

						char msg[100];
						sprintf(msg, "[Admin] Active Users: %d\n", count);
						send(clients[i].sock, msg, strlen(msg), 0);

					// Option 2
					} else if(strncmp(buffer, "2", 1) == 0) {
						log_event("Admin", "[RPC_GET_UPTIME]");

						time_t now = time(NULL);
						int uptime = (int)(now - server_start_time);

						char msg[100];
						sprintf(msg, "[Admin] Uptime: %d seconds\n", uptime);
						send(clients[i].sock, msg, strlen(msg), 0);

					// Option 3
					} else if(strncmp(buffer, "3", 1) == 0) {
						log_event("Admin", "[RPC_SHUTDOWN]");

						char *msg = "[System] EMERGENCY SHUTDOWN INITIATED\n";
						broadcast(msg, -1);

						exit(0);

					// Option 4
					} else if(strncmp(buffer, "4", 1) == 0) {
						remove_client(i);
						i--;

					// Wrong Option
					} else {
						char *msg = "[Admin] Invalid command. Please choose 1-4.\n";
						send(clients[i].sock, msg, strlen(msg), 0);
					}

					continue;
				}

				// Normal User Handler
				char buffer[BUFFER_SIZE];
				int len = recv(clients[i].sock, buffer, sizeof(buffer), 0);

				if(len <= 0) {
					remove_client(i);
					i--;
				} else {
					buffer[len] = '\0';

					if(strcmp(buffer, "/exit\n") == 0) {
						remove_client(i);
						i--;
						continue;
					}

					char msg[1200];
					sprintf(msg, "[%s]: %s", clients[i].name, buffer);

					broadcast(msg, clients[i].sock);
					log_chat(clients[i].name, buffer);
				}
			}
		}
    }

    return 0;
}
