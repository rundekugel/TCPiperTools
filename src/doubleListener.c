/*
 * tcp pipe
 * 2 listeners for connecting clients
 * 
 * 2025-02-01 written by chatGPT with rundekugel@github
 * 
 * 
 * 
 * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>

#define BUFFER_SIZE 4096
volatile int running = 1; // Steuerungsvariable für Programmbeendigung
int control_sock;
volatile int verbosity = 2;

// Struktur zur Speicherung der Socket-Paare für die Weiterleitung
struct ForwardingData {
    int *source_sock;
    int *destination_sock;
    int client_id;
};

// Funktion zum Erstellen eines Listening-Sockets
int create_listener(int port) {
    int sock;
    struct sockaddr_in server_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 10) < 0) {
        perror("listen");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}

// Funktion zum Erstellen eines nicht-blockierenden UDP-Control-Sockets
int create_control_socket(int port) {
    int sock;
    struct sockaddr_in server_addr;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("control socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind control socket");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Setze den Socket auf nicht-blockierend
    fcntl(sock, F_SETFL, O_NONBLOCK);

    return sock;
}

void logit(const char* data){
    if (control_sock >= 0) {
        send(control_sock, data, strlen(data), 0);
    }
}

// Funktion zur Weiterleitung der Daten zwischen zwei Sockets
void *forward_data(void *arg) {
    struct ForwardingData *data = (struct ForwardingData *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while (running) {
        bytes_read = recv(*(data->source_sock), buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            if(verbosity >1) {
                  printf("Client %d disconnected. Waiting for reconnection...\n", data->client_id);
                  logit("Client disconnected.\n");
            }
            *(data->source_sock) = -1;
            return NULL;
        }
        send(*(data->destination_sock), buffer, bytes_read, 0);
    }
    return NULL;
}

// Funktion zur Verarbeitung von Verbindungen und zum Starten der Datenweiterleitung
void *accept_connections(void *arg) {
    int *sockets = (int *)arg;
    int sock1 = sockets[0];
    int sock2 = sockets[1];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock1 = -1, client_sock2 = -1;
    pthread_t thread1, thread2;
    struct ForwardingData data1 = {&client_sock1, &client_sock2, 1};
    struct ForwardingData data2 = {&client_sock2, &client_sock1, 2};
    char msg[100];
    
    while (running) {
        if (client_sock1 == -1) {
            client_sock1 = accept(sock1, (struct sockaddr *)&client_addr, &client_len);
            if (client_sock1 >= 0) {
                if(verbosity >1)
                  printf("Client 1 connected.\n");
        //getpeername(client_sock1, (struct sockaddr*)&serv_addr, &len );
        //printf("Peer connected %s:%d\n", inet_ntoa(serv_addr.sin_addr),ntohs(serv_addr.sin_port));
                  
                 // snprintf(msg, sizeof(msg), "Client1 %d \n", sock1->source_sock);
    
                pthread_create(&thread1, NULL, forward_data, &data1);
                pthread_detach(thread1);
            }
        }

        if (client_sock2 == -1) {
            client_sock2 = accept(sock2, (struct sockaddr *)&client_addr, &client_len);
            if (client_sock2 >= 0) {
                if(verbosity >1)
                  printf("Client 2 connected.\n");
                pthread_create(&thread2, NULL, forward_data, &data2);
                pthread_detach(thread2);
            }
        }
        usleep(100000);
    }
    return NULL;
}

int parse_value(const char *buffer, int *out_value) {
    // Prüfen, ob die Eingabe mit "v=" beginnt
    if (buffer[0] != 'v' || buffer[1] != '=') {
        return 0; // Ungültiges Format
    }

    // Die Zahl nach dem '=' parsen
    char *endptr;
    *out_value = strtol(buffer + 2, &endptr, 10);

    // Prüfen, ob ungültige Zeichen nach der Zahl stehen
    if (*endptr != '\0' && *endptr != '\n') {
        return 0; // Ungültige Zeichen entdeckt
    }

    return 1; // Erfolgreich geparst
}

// Funktion zur Überwachung des Steuerungssockets
void *control_listener(void *arg) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (running) {
        ssize_t received = recvfrom(control_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (received > 0) {
            buffer[received] = '\0';
            if (strncmp(buffer, "STOP", 4) == 0) {
                running = 0;
                if(verbosity>0)
                  printf("Shutdown command received. Stopping server...\n");
            }
            buffer[received] = '\0';
            int v;
            int r = parse_value(buffer, &v);
            if (r) {
                verbosity = v;
                if(verbosity>0)
                  printf("verbosity changed.\n");
            }
        }
        usleep(100000); // Kurze Pause, um CPU-Last zu reduzieren
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    printf("pipe3 V0.1.0");

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <local_port1> <local_port2> <control_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int local_port1 = atoi(argv[1]);
    int local_port2 = atoi(argv[2]);
    int control_port = atoi(argv[3]);

    int listener_sock1 = create_listener(local_port1);
    int listener_sock2 = create_listener(local_port2);
    control_sock = create_control_socket(control_port);

    if(verbosity >0)
      printf("Listening on ports %d, %d (forwarding) and %d (control, UDP)...\n", local_port1, local_port2, control_port);
      logit("Listening...\n");

    pthread_t control_thread, listener_thread;
    int sockets[] = {listener_sock1, listener_sock2};
    pthread_create(&control_thread, NULL, control_listener, NULL);
    pthread_create(&listener_thread, NULL, accept_connections, sockets);

    pthread_detach(control_thread);
    pthread_detach(listener_thread);

    while (running) {
        sleep(1);
    }

    close(listener_sock1);
    close(listener_sock2);
    close(control_sock);
    return 0;
}
