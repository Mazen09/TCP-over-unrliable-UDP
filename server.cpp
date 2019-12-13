#include <utility>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#include <string>
#include <thread>
#include <ctime>
#include <bits/stdc++.h>
#include <sys/stat.h>

#define TIMEOUT 1

using namespace std;

/* Data-only packets */
struct packet {
    /* Header */
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data [500]; /* Not always 500 bytes, can be less */
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t ackno;
};


void extract_data_from_server_info();
packet create_data_packet(vector<char> data, uint32_t seqno, int data_len);
void handle_request(int client_fd, sockaddr_in client_addr, packet *packet);
void send_acknowledgement(int client_fd, sockaddr_in client_addr, uint32_t seqno);
bool do_checksum(void* vdata,size_t length);
vector<vector<char>> get_data(string file_name);
void send_data(int client_fd, struct sockaddr_in client_addr ,vector<vector<char>> data);

uint16_t checksum(void* vdata,size_t length);

bool packet_will_be_sent();

int port_number;
long random_generator_seed;
double probability_of_loss;
socklen_t sin_size = sizeof(struct sockaddr_in);
socklen_t addr_len = sizeof(struct sockaddr);

int main()
{
    int server_fd, client_fd; // listen on sock_fd, new connection on new_fd
    struct sockaddr_in server_addr{}; // my address information
    struct sockaddr_in client_addr{}; // connector’s address information
    int broadcast = 1;

    // extracting data from server.in
//    extract_data_from_server_info();
    port_number = 4566;
    probability_of_loss = 0.25;

    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    server_addr.sin_family = AF_INET; // host byte order
    server_addr.sin_port = htons(port_number); // short, network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
    memset(&(server_addr.sin_zero), '\0', 8); // zero the rest of the struct


    if ((server_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        cerr << "Can't create a socket! Quitting" << endl;
        return -6;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &broadcast, sizeof(int))) {

        perror("setsockopt server_fd:");

        return 1;

    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        cerr << "Can't bind the socket! Quitting" << endl;
        return -2;
    }

    cout << "server is ready." << endl;

    char* buf = new char[600];

    while(true){
        memset(buf,0, 600);

        int bytesReceived = recvfrom(server_fd, buf, 600, 0, (struct sockaddr*)&client_addr,  &addr_len);
        if (bytesReceived == -1){
            perror("Error in recv(). Quitting\n");
            return -5;
        }
        if (bytesReceived == 0){
            perror("Client disconnected !!! \n");
            return-5;
        }

        auto* data_packet = (struct packet*) buf;

        //delegate request to child process
        pid_t pid = fork();

        if (pid == 0)
        {
           //child process
            if ((client_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
                cerr << "Can't create a socket! Quitting" << endl;
                return -6;
            }
            if (setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &broadcast, sizeof(int))) {

                perror("setsockopt server_fd:");

                return 1;
            }

            handle_request(client_fd, client_addr, data_packet);
        }
        if (pid > 0)
        {
             //parent process
            continue;
        }


    }
}

void handle_request(int client_fd, sockaddr_in client_addr, packet *packet) {

    vector<vector<char>> data;
    if(do_checksum((void*)packet, sizeof(struct packet))){

        int data_length = packet->len - sizeof(packet->seqno) - sizeof(packet->len)
                   - sizeof(packet->cksum);
        string file_name = string(packet->data, 0, data_length);
        cout << "file name: "<<string(packet->data, 0, data_length)<<endl;
        data = get_data(file_name);
        send_data(client_fd, client_addr, data);
    }
}

vector<vector<char>> get_data(string file_name){
    vector<vector<char>> t;
    vector<char> d;
    char c;
    ifstream fin;
    string path = getenv("HOME");
    fin.open(path +"/"+file_name);

    if(fin){
        int counter = 0;
        while(fin.get(c)){
            if(counter < 499) {
                d.push_back(c);
            }else{
                t.push_back(d);
                d.clear();
                d.push_back(c);
                counter = 0;
                continue;
            }
            counter++;
        }
        if(counter > 0) t.push_back(d);
    }else{
        perror("couldn't find the file requested");
        exit(1);
    }
    fin.close();

    return t;
}

void send_data(int client_fd, struct sockaddr_in client_addr , vector<vector<char>> data) {
    map<uint32_t, vector<char>> sent_and_waits_for_ack;
    int cwnd = 1;
    int seqno ;
    for (int i = 0; i < data.size(); ++i) {
        seqno = 500*i;
        struct packet p = create_data_packet(data.at(i), seqno, data.at(i).size());
        cout <<"data size = " << data.at(i).size()<<endl;

        char* buf = new char[600];
        memset(buf, 0, 600);
        memcpy(buf, &p, sizeof(p));

        if(packet_will_be_sent()){
            int bytesSent = sendto(client_fd, buf, 600, 0,(struct sockaddr *)&client_addr, sizeof(struct sockaddr));
            if (bytesSent == -1) {
                perror("couldn't send the ack");
                exit(1);
            }
        }
        int sret;
        fd_set readfds;
        struct timeval timeout{};

        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);

        timeout.tv_sec = TIMEOUT;
        timeout.tv_usec = 0;

        sret = select(client_fd+1, &readfds, nullptr, nullptr, &timeout);

        if(sret == 0){
            cout << "No acks received for packet "<< i << ". Resending" << endl;
            i--;
            continue;

        }else if(sret == -1){
            perror("error in select");
            exit(1);
        }else{
            memset(buf,0, 600);
            int bytesReceived = recvfrom(client_fd, buf, 600, 0, (struct sockaddr*)&client_addr,  &addr_len);
            if (bytesReceived == -1){
                perror("Error in recv(). Quitting");
                exit(1);
            }
            cout <<"ack received for packet "<< i <<endl;
        }
    }
}

packet create_data_packet(vector<char> data, uint32_t seqno, int data_len) {
    struct packet p;
    p.len = data_len + sizeof(p.cksum) + sizeof(p.len) + sizeof(p.seqno);
    p.seqno = seqno;
    memset(p.data, 0, 500);
    strcpy(p.data, data.data());
    p.cksum = checksum((void*)&p, sizeof(p));

    return p;
}

void extract_data_from_server_info(){
    ifstream fin;
    string path = getenv("HOME");
    path.append("/server.in");
    fin.open(path);

    if(fin){
        string line;

        getline(fin, line);
        port_number = stoi(line);

        getline(fin, line);
        random_generator_seed = stol(line);

        getline(fin, line);
        probability_of_loss = stod(line);

        fin.close();
    }else{
        cerr << "Can't find server.in Quitting" << endl;
    }
}


bool do_checksum(void* vdata,size_t length) {
//    return (checksum(vdata, length) == 0);
    return true;
}

uint16_t checksum(void* vdata,size_t length) {
/*    // Cast the data pointer to one that can be indexed.
    char* data=(char*)vdata;

    // Initialise the accumulator.
    uint32_t acc=0xffff;

    // Handle complete 16-bit blocks.
    for (size_t i=0;i+1<length;i+=2) {
        uint16_t word;
        memcpy(&word,data+i,2);
        acc+=ntohs(word);
        if (acc>0xffff) {
            acc-=0xffff;
        }
    }

    // Handle any partial block at the end of the data.
    if (length&1) {
        uint16_t word=0;
        memcpy(&word,data+length-1,1);
        acc+=ntohs(word);
        if (acc>0xffff) {
            acc-=0xffff;
        }
    }

    // Return the checksum in network byte order.
    return htons(~acc);
    */
    return 0;
}

bool packet_will_be_sent() {
    srand(time(0));
    return rand() % 100 > probability_of_loss * 100;
}