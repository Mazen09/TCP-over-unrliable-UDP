//client.cpp
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

#define TIMEOUT 5

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


using namespace std;

packet create_data_packet(const string& file_name);
void send_file_name_packet(char* buf);
uint16_t checksum(void* vdata,size_t length);
bool do_checksum(void* vdata,size_t length);
void extract_data(struct packet *data_packet);
void send_acknowledgement(uint32_t seqno);
void *client();
string generate(int max_length);

int sockfd;
struct sockaddr_in serv_addr;
int port_number;
socklen_t addr_len = sizeof(struct sockaddr);
map<uint32_t, vector<char>> file_chunks;

int main() {
    int NUM_THREADS = 3;
    pthread_t threads[NUM_THREADS];
    int rc;
    int i;

    for( i = 0; i < NUM_THREADS; i++ ) {
        cout << "main() : creating thread, " << i << endl;
        rc = pthread_create(&threads[i], NULL, reinterpret_cast<void *(*)(void *)>(client), NULL);

        if (rc) {
            cout << "Error:unable to create thread," << rc << endl;
            exit(-1);
        }
    }
    pthread_exit(NULL);

//    client();
}

void *client(){
    memset(&serv_addr, 0, sizeof(serv_addr));
    // reading client.in
    ifstream fin;
    ofstream fout;
    std::string home_path = getenv("HOME");
    std::string file_name;
    std::string line;

//    fin.open(home_path + "/client.in");
//    if(fin)
//    {
//        getline(fin, line);
//        port_number = stoi(line);
//        getline(fin, file_name);
//    }
//    fin.close();


    port_number = 4566;
    file_name = "input.txt";


    // server info
    serv_addr.sin_family = AF_INET; // host byte order
    serv_addr.sin_port = htons(port_number); // short, network byte order
    serv_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
    memset(&(serv_addr.sin_zero), '\0', 8); // zero the rest of the struct

    // create client socket
    if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        cerr << "Can't create a socket! Quitting" << endl;
        exit(EXIT_FAILURE);
    }

    // send a packet with the filename
    struct packet p = create_data_packet(file_name);

    char* buf = new char[600];
    memset(buf,0, 600);
    memcpy(buf, &p, sizeof(p));

    send_file_name_packet(buf);

    //receive the data

    int i = 0;
    while(true)
    {
        int sret;
        fd_set readfds;
        struct timeval timeout{};

        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        timeout.tv_sec = TIMEOUT;
        timeout.tv_usec = 0;

        sret = select(sockfd+1, &readfds, nullptr, nullptr, &timeout);

        if(sret == 0){
            cout << "No packets comes from the server! \n Aborting..." << endl;
            break;
        }else if(sret == -1){
            perror("error in select");
            exit(1);
        }

        memset(buf,0, 600);
        int bytesReceived = recvfrom(sockfd, buf, 600, 0, (struct sockaddr*)&serv_addr,  &addr_len);
        if (bytesReceived == -1){
            perror("Error in recv(). Quitting");
            exit(EXIT_FAILURE);
        }

        cout <<"packet "<<i<<" received" <<endl;
        //put it into packet
        auto* data_packet = (struct packet*) buf;
        if(do_checksum((void*)data_packet, sizeof(packet))){
            extract_data(data_packet);
            send_acknowledgement(data_packet->seqno);
        }
        i++;
    }

    string opfile = generate(10) ;
    fout.open(home_path + "/" +opfile+".txt");
    cout <<"map size: "<< file_chunks.size() << endl;
    map<uint32_t, vector<char>> :: iterator it;
    cout << "full map: \n[ ";
    for (it=file_chunks.begin() ; it!=file_chunks.end() ; it++){
        for(char c : (*it).second)
        {
            fout << c;
            cout<< c;
        }
    }
    fout.close();
    cout << "]";
}


void send_acknowledgement(uint32_t seqno) {
    struct ack_packet ack;
    ack.cksum = 0;
    ack.len = sizeof(ack);
    ack.ackno = seqno;
    ack.cksum = checksum((void*)&ack, sizeof(struct ack_packet));

    char* buf = new char[600];
    memset(buf,0, 600);
    memcpy(buf, &ack, sizeof(ack));

    int bytesSent = sendto(sockfd, buf, 600, 0,(struct sockaddr *)&serv_addr, sizeof(struct sockaddr));
    if (bytesSent == -1) {
        perror("couldn't send the ack");
        exit(1);
    }
}

void extract_data(struct packet *data_packet) {
    uint32_t seqno = data_packet->seqno;
    vector<char> data;

    int size = data_packet->len - sizeof(seqno) - sizeof(data_packet->len)
               - sizeof(data_packet->cksum);
    cout <<"data size = " << size << endl;
    data.reserve(size);

for (int i = 0; i < size; ++i) {
        data.push_back(data_packet->data[i]);
    }

    file_chunks.insert(make_pair(seqno, data));
    cout <<"chunk size: "<< data.size() << ", seqno: "<<seqno << endl;
    cout <<"data:\n[ ";
    for(char c : data){
        cout<< c;
    }
    cout <<"]\n\n";
}

bool do_checksum(void* vdata,size_t length) {
//    return (checksum(vdata, length) == 0);
    return true;
}

packet create_data_packet(const string& file_name) {
    struct packet p{};
    p.seqno = 0;
    p.len = file_name.length() + sizeof(p.cksum) + sizeof(p.len) + sizeof(p.seqno);
    strcpy(p.data, file_name.c_str());
    p.cksum = checksum((void*)&p, sizeof(struct packet));
    return p;
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


void send_file_name_packet(char* buf){
    int sret;
    fd_set readfds;
    struct timeval timeout{};

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;

    int bytesSent = sendto(sockfd, buf, 600, 0,(struct sockaddr *)&serv_addr, sizeof(struct sockaddr));
    if (bytesSent == -1) {
        perror("couldn't send the packet");
    }

    sret = select(sockfd+1, &readfds, nullptr, nullptr, &timeout);

    if(sret == 0){
        cout << "No response from the server! \n Resending packet..." << endl;
        send_file_name_packet(buf);
    }else if(sret == -1){
        perror("error in select");
        exit(1);
    }
}

string generate(int max_length){
    string possible_characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    random_device rd;
    mt19937 engine(rd());
    uniform_int_distribution<> dist(0, possible_characters.size()-1);
    string ret = "";
    for(int i = 0; i < max_length; i++){
        int random_index = dist(engine); //get index between 0 and possible_characters.size()-1
        ret += possible_characters[random_index];
    }
    return ret;
}