#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 5
#define MAX_QUEUE_SIZE 100
#define UNLOCK 0
#define LOCK 1
#define SERVER_ID 7
#define REQUEST 1
#define PORT 8087

//Structure to store clients' REQUEST messages
struct Request{
    int message;
    time_t timestamp;
    int client_id;
};

//Request queue structure
struct Req_Queue {
    int client_fd;
    time_t timestamp;
    int client_id;
};

//Function prototypes
void *receive_request(void *arg);
void enqueue_request(struct Request req, int client_fd);
void send_grant(int clientid, int client_fd);
void dequeue_request(int client_id);
void receive_release (int clientsock_fd);
void next_up(); 

int client_sockets[MAX_CLIENTS];
pthread_t client_threads[MAX_CLIENTS];
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
struct Req_Queue req_queue[MAX_QUEUE_SIZE];
int queue_size = 0;
int server_state = UNLOCK;
int locking_client;

//Receive REQUEST message from clients
void *receive_request(void *arg) {
    int client_socket = *(int *)arg;
    struct Request buffer;
    int read_size;
    
    if ((read_size = recv(client_socket, &buffer, sizeof(struct Request), 0)) > 0) {
        
        printf("\n REQUEST received from client %d", buffer.client_id);   
    }
    else if (read_size == -1) {
        perror("recv failed");
    }
    else printf("\n No data received");

    //If REQUEST is received, enqueue into request queue
    if (buffer.message == REQUEST) 
        enqueue_request(buffer, client_socket);
    //If RELEASE is received, dequeue from request queue
    else {
        printf("\n RELEASE");
        dequeue_request(buffer.client_id);
    }
}

void enqueue_request(struct Request req, int client_fd) {
    printf("\n Enqueueing request");
    
    //If the request queue is full, drop the new REQUEST
    if (queue_size == MAX_QUEUE_SIZE) {
        printf("Queue is full, dropping request from client %d\n", req.client_id);
        pthread_exit(NULL);
        return;
    }
    
    pthread_mutex_lock(&queue_mutex);
    //If queue has no requests, write new REQUEST into queue
    int i = 0;
    if(queue_size == 0) {
        req_queue[0].client_fd = client_fd;
        req_queue[0].timestamp = req.timestamp;
        req_queue[0].client_id = req.client_id;
    }
    
    else {
        for (i = 0; i< queue_size; i++) {
            //Order the REQUESTs by their timestamp
            if (difftime(req.timestamp,req_queue[i].timestamp) < 0) {        
                printf("\n New time less than existing");
                int j;
                //Shift larger timestamps by one position
                for(j = queue_size; j > i; j--) {
                    req_queue[j+1] = req_queue[j];
                }
                //Insert new REQUEST into correct position in queue
                req_queue[i].client_fd = client_fd;
                req_queue[i].timestamp = req.timestamp;
                req_queue[i].client_id = req.client_id; 
                queue_size++;      
                break;    
            }     
        }
    }
    
    if (i == queue_size) {
        req_queue[queue_size].client_fd = client_fd;
        req_queue[queue_size].timestamp = req.timestamp;
        req_queue[queue_size].client_id = req.client_id; 
        queue_size++;
    }
    pthread_mutex_unlock(&queue_mutex);

    //Print queue details
    printf("\n Queue size %d", queue_size);

    int k;
    printf("\n Request Queue:");
    for(k = 0; k < queue_size; k++)  {
        printf("\t  %d ", req_queue[k].client_id);
    }
    
    send_grant(req.client_id, client_fd);

    receive_release(client_fd);
}

void send_grant(int clientid, int client_fd) {

    //If the server is unlocked, send GRANT to requesting client
    if (server_state == UNLOCK) {
        int buffer = SERVER_ID;
        printf("\n Sending grant");
        
        if (queue_size != MAX_QUEUE_SIZE) {
            int bytes_tx = 0;
            if ((bytes_tx = write(client_fd, &buffer, sizeof(buffer))) > 0) {
                //Dequeue the client's REQUEST from the request queue 
                dequeue_request(clientid);
                
                pthread_mutex_lock(&queue_mutex);
                //LOCK the server so it cannot send further GRANTs until it is unlocked
                server_state = LOCK;
                //Set the client as the locking client
                locking_client = clientid;
                pthread_mutex_unlock(&queue_mutex);

                printf("\n GRANT sent! Server locked by client %d", clientid);
            }
            else if (bytes_tx == 0) 
                printf ("\n Grant not sent");
            else 
                perror("Unable to send grant");    
        }  
        else printf("\n Queue is full!");
    }
    else printf("Server is locked. Unable to send grant to client %d", clientid);
}

void dequeue_request(int client_id) {
    
    printf("\n Dequeueing client %d's REQUEST from queue", client_id);
    
    pthread_mutex_lock(&queue_mutex);
    //Remove the client from the request queue
    int i;
    for (i = 0; i < queue_size; i++) {
        if (req_queue[i].client_id == client_id) { 
            int j;
            for (j = i; j < queue_size; j++) {
                req_queue[j] = req_queue[j + 1];
            }
        break;    
        }       
    }
    pthread_mutex_unlock(&queue_mutex);

    int k;
    printf("\n Updated request Queue:");
    for(k = 0; k < queue_size; k++)  {
        printf("\t %d ", req_queue[k].client_id);
    }
    
    if (i == queue_size) {
        //If client ID is not found in the queue, send GRANT to next client in the request queue
        printf("Client %d not found in queue\n", client_id);
        next_up();
    } 
    else printf("\n Dequeued Client %d", client_id);

    queue_size--;
}

void receive_release (int clientsock_fd) {
    int release_buf = 0;
    int rx_bytes;
    
    //Receive RELEASE message from client
    if((rx_bytes = recv(clientsock_fd, &release_buf, sizeof(release_buf), 0)) > 0) {
        printf("\n Received RELEASE from client %d", release_buf);
        //If RELEASE is sent by the client that locked the server, 
        //unlock the server and send GRANT to next request in queue
        if (release_buf == locking_client)  {
            server_state = UNLOCK;
            locking_client = UNLOCK;
            printf ("\n Server unlocked by client %d", release_buf);
            if ((queue_size != 0) && (locking_client == UNLOCK)) {
                printf("\n Next in the queue:");
                next_up();
            }
        }       
        //If RELEASE is sent by a different client, delete that client's request from queue
        else
            dequeue_request(release_buf);
    }
    else if (rx_bytes < 0)
        perror("Recv failed!");
    else printf ("\n Client disconnected!");
}

void next_up() {
    //Read top of the request queue
    pthread_mutex_lock(&queue_mutex);
    int next_client, next_client_fd;
    next_client = req_queue[0].client_id;
    next_client_fd = req_queue[0].client_fd;
    pthread_mutex_unlock(&queue_mutex);

    if (next_client == 0) {
        printf("   No other requests in queue");
    }   
    else {
        printf(" %d", next_client);
        //Send GRANT to the top request in queue
        if(server_state == UNLOCK)
            send_grant(next_client, next_client_fd);   
    }
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
    while(1) {
        int j;
        for (j = 0; j < MAX_CLIENTS; j++) {
            socklen_t addrlen = sizeof(server_addr);
            if ((client_socket = accept(server_socket, (struct sockaddr *)&server_addr, &addrlen)) == -1) {
                perror("accept failed");
                continue;
            }
            else printf("\n \n Client connected: %d\n", client_socket);
            
            //Save client file descriptors in an array for further read/write operations
            client_sockets[j] = client_socket;
        
            //Create a thread to handle every client
            if (pthread_create(&client_threads[j], NULL, receive_request, (void *)&client_sockets[j]) < 0) {
                perror("thread creation failed");
            }
        }

        //Join all open threads
        int i;
        for (i = 0; i < MAX_CLIENTS; i++) {
            pthread_join(client_threads[i], NULL);
        }
    }
    
    //Close the client sockets
    int k=0;
    for (k =0; k < MAX_CLIENTS; k++)  {
        close(client_sockets[k]);
    }

    //Close server socket
    close(server_socket);
    return 0;
}
