#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

#define MAX_SERVERS 7
#define CLIENT_ID 2
#define REQUEST 1
#define s0_PORT 8080
#define TOTAL_RUNS 20

pthread_mutex_t grant_lock = PTHREAD_MUTEX_INITIALIZER;

struct ClientRequest {
    int message;
    time_t timestamp;
    int client_id;
};

//Array to save received GRANTs
int Grant_array[MAX_SERVERS];
//Count of number of times critical section has been executed
int CS_count = 0;
//Flag to indicate if critical section can be executed
int token = 0;
//Flag to indicate if release messages can be sent
int release_token = 0;
//Counter for all REQUEST messages sent by client
int REQ_count = 0;
int Crit_REQ_count = 0;
//Counter for all GRANT messages received by client
int GRANT_count = 0;
//Counter for all RELEASE messages sent by client
int REL_count = 0;
int tot_msgs = 0;
time_t req_time;

int QuorumSet [15][MAX_SERVERS] = {
        {1,1,0,1,0,0,0}, //1,2,4
        {1,1,0,0,1,0,0}, //1,2,5
        {1,0,0,1,1,0,0}, //1,4,5
        {1,0,1,0,0,1,0}, //1,3,6
        {1,0,1,0,0,0,1}, //1,3,7
        {1,0,0,0,0,1,1}, //1,6,7
        {0,1,1,1,0,1,0}, //2,3,4,6
        {0,1,1,1,0,0,1}, //2,3,4,7
        {0,1,0,1,0,1,1}, //2,4,6,7
        {0,1,1,0,1,1,0}, //2,3,5,6
        {0,1,1,0,1,0,1}, //2,3,5,7
        {0,1,0,0,1,1,1}, //2,3,6,7
        {0,0,1,1,1,1,0}, //3,4,5,6
        {0,0,1,1,1,0,1}, //3,4,5,7
        {0,0,0,1,1,1,1}  //4,5,6,7
};


// Function prototypes
void *send_to_server(void *arg);
void *receive_grant (void *sock);
void CS_execute(int client_id);
void *send_release(void *socketfd);
void Completion();
void Grant_compare();

// Struct to hold server information
typedef struct {
    char *ip;
    int port;
} server_info;

// Array of server information
server_info servers[MAX_SERVERS] = {
    {"10.176.69.32", 8081},
    {"10.176.69.33", 8082},
    {"10.176.69.34", 8083},
    {"10.176.69.35", 8084},
    {"10.176.69.36", 8085},
    {"10.176.69.37", 8086},
    {"10.176.69.38", 8087},
};

//Thread function to send request to a server
void *send_request(void *arg) {
    int sock = *(int *)arg;
    int n;
    struct ClientRequest request;
    
    // Send request to the server
    request.message = REQUEST;
    request.timestamp = time(NULL);
    request.client_id = CLIENT_ID;
    
    //Send the REQUEST struct to server
    n = write(sock, &request, sizeof(request));
    if (n < 0) {
        perror("ERROR writing to socket");
        pthread_exit(NULL);
    }

    else {
        //Increment the count of REQUESTs sent
        REQ_count++;
        Crit_REQ_count++;
        time(&req_time);
        printf("\n Sent REQUEST to server FD %d at time %s", sock, ctime(&req_time));
    }  
    return NULL;
}

//Thread function to receive GRANTs from a server
void *receive_grant (void *sock) {
    
    int client_fd = *(int *)sock;
    int buffer_grant = 0;
    int read_bytes = 0;

    read_bytes = recv(client_fd, &buffer_grant, sizeof(buffer_grant), MSG_DONTWAIT); 
    if (read_bytes < 0) {
        pthread_exit(NULL);
    }
    else if (read_bytes > 0) {
        printf("\n Received GRANT from server %d", buffer_grant);
        //Increment the count of GRANTs received 
        GRANT_count++;
        int i;
        //Store the received GRANT in an array
        for(i=0; i <= MAX_SERVERS; i++) {
            if ((i+1) == buffer_grant) {
                Grant_array[i] = 1;
                break;
            }
        }

        //Print the grant array
        printf("\n Grant array :");
        int j;
        for (j = 0; j < MAX_SERVERS; j++)
            printf("\t %d", Grant_array[j]);
    }   
    else printf("\n No reply from server");
}

void Grant_compare() {
    //Check if GRANTs form a quorum
    printf("\n Checking if received GRANTS form a quorum");
    int n;
    int req_grants = 0;
    int count = 0;
    
    for(n=0; n < 15; n++) {
        int j;
        for(j=0; j < MAX_SERVERS; j++) {
            if (QuorumSet[n][j] == 1) {
                req_grants++; 
                if(Grant_array[j] == 1)
                    count++; 
                else {
                    count = 0;
                }                        
            }      
        }
        
        if ((count == req_grants) && (count != 0) && (req_grants != 0)) {
            printf ("\n GRANTs form a quorum!");
            pthread_mutex_lock(&grant_lock);
            token = 1;
            pthread_mutex_unlock(&grant_lock);
            count = 0;
            req_grants = 0;
            break;
        }

        else {
            count = 0;
            req_grants = 0;
        }
    }
}

void CS_execute(int client_id) {  
    printf("\n \n Entering");
    //Get current time
    time_t critsec_time;
    time(&critsec_time);

    //Print client ID and current time
    printf("\n Client ID: %d, Current time: %s\n", client_id, ctime(&critsec_time));
    //Wait for 3 time units
    usleep(3);

    CS_count++;
    printf("\n \n Critical section executed %d times \n \n ", CS_count);

    //Calculate time taken between sending a round of REQ msgs and entering critical section
    double time_diff = difftime(critsec_time, req_time);
    printf("\n \n Time elapsed between REQ and entering critical section (Latency): %f seconds \n", time_diff); 
    
    //Calculate number of GRANTs required to execute critical section
    int j, crit_grants = 0;
    for (j = 0; j < MAX_SERVERS; j++) {
        if(Grant_array[j] == 1)
            crit_grants++;
    }
    //Number of msgs required to enter critical section = REQ + GRANT
    tot_msgs = (Crit_REQ_count + crit_grants);  
    printf("\n \n Total number of messages exchanged to enter critical section: %d\n", tot_msgs);

    //Allow RELEASE messages to be sent to all servers
    release_token = 1;
    //Reset permission to enter critical section 
    token = 0;
}

void *send_release(void *socketfd) {
    int rel_sock = *(int *)socketfd;
    int release_buffer = CLIENT_ID;
    int read_bytes; 

    //Send RELEASE to all servers
    if ((read_bytes = write(rel_sock, &release_buffer, sizeof(release_buffer)) ) < 0) {
        perror("\n ERROR writing release");
        pthread_exit(NULL);
    }
    else if (read_bytes > 0) {
        REL_count++;
        printf("\n Release sent to server fd %d!", rel_sock);
    }
    else printf ("\n Disconnected!");
}

void Completion() {
    //Connect with server 0 to send completion notification
    int s0_sock;
    struct sockaddr_in s0_server_addr;
    //Create socket
    s0_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (s0_sock < 0) {
        perror("ERROR opening socket for S0");
        pthread_exit(NULL);
    } 

    //Set server address
    memset(&s0_server_addr, 0, sizeof(s0_server_addr));
    s0_server_addr.sin_family = AF_INET;
    s0_server_addr.sin_port = htons(s0_PORT);

    //Connect to server inet address
    if (inet_pton(AF_INET, "10.176.69.39", &s0_server_addr.sin_addr) <= 0) {
        perror("Invalid address!");
        exit(EXIT_FAILURE);
    }
    //Connect to server
    if (connect(s0_sock, (struct sockaddr*) &s0_server_addr, sizeof(s0_server_addr)) < 0) {
        perror("ERROR connecting");
        exit(EXIT_FAILURE);
    }

    //Send Completion notification to server0
    int completion_buffer = CLIENT_ID;
    int s0_bytes;
    if ((s0_bytes = send(s0_sock, &completion_buffer, sizeof(completion_buffer), 0)) > 0) {
        printf("\n Sent COMPLETION NOTIFICATION to server 0 \n");
    }
    else if (s0_bytes < 0) {
        perror("ERROR writing to socket 0");
        pthread_exit(NULL);
    }
    else printf("\n Unable to write to Server 0");

    close(s0_sock);
}

int main() {
    int i, sock[MAX_SERVERS];
    pthread_t threads[MAX_SERVERS];
    pthread_t release_threads[MAX_SERVERS];
    pthread_t grant_threads[MAX_SERVERS];

    //If critical section has not been executed more than 20 times, keep communicating with servers
    while(CS_count <= TOTAL_RUNS) {

        //If critical section has been executed 20 times, connect to server 0
        if(CS_count == TOTAL_RUNS) {
            printf("\n \n Critical Section executed 20 times!");
            Completion();
            break;
        }
        
        //Wait before sending REQUEST to all servers
        srand(time(NULL));
        time_t wait_time = (rand() % 6) + 5;
        usleep(wait_time);  
        
        //Create socket for each server
        for (i = 0; i < MAX_SERVERS; i++) {
            // Create a socket
            sock[i] = socket(AF_INET, SOCK_STREAM, 0);
            if (sock[i] == -1) {
                perror("socket() failed");
                exit(EXIT_FAILURE);
            }

            //Server address information
            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            //Pass server addresses stored in servers array
            server_addr.sin_port = htons(servers[i].port);
            if (inet_pton(AF_INET, servers[i].ip, &server_addr.sin_addr) <= 0) {
                perror("inet_pton() failed");
                exit(EXIT_FAILURE);
            }

            //Connect to server
            if (connect(sock[i], (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
                perror("connect() failed");
                exit(EXIT_FAILURE);
            }

            //Create thread to send REQUEST to server
            pthread_create(&threads[i], NULL, send_request, (void *)&sock[i]);
        } 

        //Join all request threads
        for (i = 0; i < MAX_SERVERS; i++) {
            pthread_join(threads[i], NULL);
        }

        while(!token) {
            int j;
            //Create thread to receive GRANT from servers
            for (j = 0; j < MAX_SERVERS; j++) {
                pthread_create(&grant_threads[j], NULL, receive_grant, (void *)&sock[j]);
            }

            //Join all GRANT threads
            for (j = 0; j < MAX_SERVERS; j++) {
                pthread_join(grant_threads[j], NULL);
            }

            //Call function to check if GRANTs received form a quorum
            Grant_compare();
        }
        

        //Execute critical section if received GRANTs form a quorum
        if (token) {
            int client_num;
            client_num = CLIENT_ID;
            CS_execute(client_num);
        }

        //Send RELEASE to all servers if critical section has been executed
        if(release_token) {    
            int k;
            for (k = 0; k < MAX_SERVERS; k++) {
                //Create thread to send RELEASE to server
                pthread_create(&release_threads[k], NULL, send_release, (void *)&sock[k]);
            }

            //Join all RELEASE threads
            for (k = 0; k < MAX_SERVERS; k++) {
                pthread_join(release_threads[k], NULL);
            }
        }   

        Crit_REQ_count = 0;
        release_token = 0;
        //Clear the grant array
        memset(Grant_array, 0, sizeof(Grant_array));

        //Close all server sockets
        for (i = 0; i < MAX_SERVERS; i++) {
            close(sock[i]);
        }
    }

    printf("\n Total number of REQUESTs sent: %d\n", REQ_count);
    printf("\n Total number of RELEASEs sent: %d\n", REL_count);
    printf("\n Total number of messages sent: %d\n", REQ_count+REL_count);
    printf("\n Total number of GRANTs received: %d\n", GRANT_count);
    return 0;
}