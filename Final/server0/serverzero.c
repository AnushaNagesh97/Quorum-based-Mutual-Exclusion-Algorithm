#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080              
#define MAX_CLIENTS 5         

pthread_t client_threads[MAX_CLIENTS];
int client_sockets[MAX_CLIENTS];
int completion_count;

void *receive_notification(void *arg) {
    int client_socket = *(int *)arg;
    int completion_buffer=0;
    int read_size =0;
    
    if ((read_size = recv(client_socket, &completion_buffer, sizeof(completion_buffer), 0)) > 0) {
        printf("\n COMPLETION NOTIFICATION received from client %d", completion_buffer);  
        completion_count++;
    }
    else if (read_size == -1) {
        perror("recv failed");
    }
    else printf("\n No data received");

    if(completion_count == MAX_CLIENTS)
        printf("\n All clients have completed!\n");
}

int main(int argc, char *argv[]) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr;
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed");
        return 1;
    }
    memset(&server_addr, 0, sizeof(server_addr));

    //Make socket address reuseable
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt failed!");
        exit(EXIT_FAILURE);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        return 1;
    }
    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("listen failed");
        return 1;
    }
    printf("Server started\n");
    int j;
    for (j = 0; j < MAX_CLIENTS; j++) {
        socklen_t addrlen = sizeof(server_addr);
        if ((client_socket = accept(server_socket, (struct sockaddr *)&server_addr, &addrlen)) == -1) {
            perror("accept failed");
            continue;
        }

        //Save client file descriptors for further read/write operations    
        client_sockets[j] = client_socket;
        
        //Create a thread to handle each client concurrently
        if (pthread_create(&client_threads[j], NULL, receive_notification, (void *)&client_sockets[j]) < 0) {
            perror("thread creation failed");
        }
    }
      
    for (j = 0; j < MAX_CLIENTS; j++) {
        pthread_join(client_threads[j], NULL);
    }

    int k=0;
    for (k =0; k < MAX_CLIENTS; k++)  {
        close(client_sockets[k]);
    }

    close(server_socket);
    return 0;
}
