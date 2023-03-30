#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "log.h"
#include "packet_implem.c"
#include "packet_interface.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

typedef struct node {
    pkt_t * pkt;
    size_t len;
    struct node * next;
} node_t;

typedef struct win
{
    node_t *first;
    int start;
    int end;
} win_t;

int modulo(int a, int b){
    int diff = (b-a) % 256;
    if(diff < 0){
        return 256 + diff;
    }
    return diff;
}

int time_check(int a, int b){
    int diff = (b - a) % 4294967296;
    if(diff < 0){
        return diff + 4294967296;
    }
    return diff;
}

int ack_check(win_t * win, int ack_seq){
    if(win->start < win->end){
        if(win->start <= ack_seq && ack_seq <= win->end+1){
            return 1;
        }
        return 0;
    }
    if(win->start <= ack_seq){
        return 1;
    }
    if(win->end + 1 >= ack_seq){
        return 1;
    }
    return 0;
}


win_t *init()
{
    win_t *window = malloc(sizeof(win_t));
    node_t *first = malloc(sizeof(node_t));

    if (window == NULL || first == NULL)
    {
        exit(EXIT_FAILURE);
    }

    window->first = first;
    window->start = 0;
    window->end = 0;

    return window;
}


void push(win_t * win, pkt_t * pkt) {
    node_t * iter = win->first;
    win->end = pkt_get_seqnum(pkt);

    while (iter->next != NULL) iter = iter->next;

    iter->next = (node_t *) malloc(sizeof(node_t));
    iter->next->pkt = pkt;
    iter->next->next = NULL;
}

pkt_t * pop(win_t * win) {

    node_t * iter = NULL;

    if (win->first == NULL) {
        return NULL;
    }
    if(win->end == win->start){
        win->start = (win->start + 1) % 256;
        win->end = (win->end + 1) % 256;
        return NULL;
    }
    iter = win->first->next;
    pkt_t * packet = win->first->pkt;
    free(win->first);
    win->first = iter;
    win->start = (win->start + 1) % 256;

    return packet;
}

pkt_t * get(win_t * win, int k) {
    node_t * iter = NULL;
    if (win->first == NULL) {
        return NULL;
    }
    iter = win->first;
    int dif = modulo(win->start, k);
    for (int i = 0; i <= dif; i++){
        iter = iter->next;
    }
    pkt_t * packet = iter->pkt;

    return packet;
}

int print_usage(char *prog_name) {
    ERROR("Usage:\n\t%s [-f filename] [-s stats_filename] receiver_ip receiver_port", prog_name);
    return EXIT_FAILURE;
}

uint32_t max(uint32_t a, uint32_t b){
    if(a > b) return a;
    return b;
}
uint32_t min(uint32_t a, uint32_t b){
    if(a < b) return a;
    return b;
}

int main(int argc, char **argv) {
    int opt;

    char *filename = NULL;
    char *stats_filename = NULL;
    char *receiver_ip = NULL;
    char *receiver_port_err;
    uint16_t receiver_port;

    while ((opt = getopt(argc, argv, "f:s:h")) != -1) {
        switch (opt) {
        case 'f':
            filename = optarg;
            break;
        case 'h':
            return print_usage(argv[0]);
        case 's':
            stats_filename = optarg;
            break;
        default:
            return print_usage(argv[0]);
        }
    }

    if (optind + 2 != argc) {
        ERROR("Unexpected number of positional arguments");
        return print_usage(argv[0]);
    }

    receiver_ip = argv[optind];
    receiver_port = (uint16_t) strtol(argv[optind + 1], &receiver_port_err, 10);
    if (*receiver_port_err != '\0') {
        ERROR("Receiver port parameter is not a number");
        return print_usage(argv[0]);
    }

    ASSERT(1 == 1); // Try to change it to see what happens when it fails
    DEBUG_DUMP("Some bytes", 11); // You can use it with any pointer type

    // This is not an error per-se.
    ERROR("Sender has following arguments: filename is %s, stats_filename is %s, receiver_ip is %s, receiver_port is %u",
        filename, stats_filename, receiver_ip, receiver_port);

    DEBUG("You can only see me if %s", "you built me using `make debug`");
    ERROR("This is not an error, %s", "now let's code!");

    // Now let's code!

    int sock = socket(AF_INET6, SOCK_DGRAM, 0); 
    if(sock == -1){
        return -1;
    }

    struct sockaddr_in6 dest_addr;
    bzero(&dest_addr, sizeof (struct sockaddr_in6));
    
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(receiver_port);

    if (inet_pton(AF_INET6, receiver_ip, &dest_addr.sin6_addr) != 1){
        return -1;
    }

    int err = connect(sock, (const struct sockaddr *) &dest_addr, sizeof(struct sockaddr_in6)); 
    if (err == -1) {
        return -1;
    }

    

    int data_sent = 0;
    int ack_received = 0;
    int nack_received = 0;
    int packet_ignored = 0;
    int packet_retransmitted = 0;
    int last_pkt = -2;
    int last_ack = -1;
    uint32_t max_rtt = 0;
    uint32_t min_rtt = 2000000000;
    
    int fd = STDIN_FILENO;
    if(filename != NULL) fd = open(filename, O_RDONLY);
    ptypes_t type = PTYPE_DATA;
    uint8_t tr = 0;
    uint8_t window = 0;
    uint8_t seqnum = 0;
    uint32_t crc1 = 0;
    uint32_t crc2 = 0;
    win_t * win = init();
    fd_set rfds;
    FD_ZERO(&rfds);

    clock_t timestamp_start = clock();
    clock_t timestamp_end;

    while (1 == 1){
        timestamp_end = clock();
        if(last_ack != seqnum && time_check((uint32_t)timestamp_start,(uint32_t) timestamp_end) >= 200000){
            pkt_t * resent = get(win , win->end);
            timestamp_start = clock();
            pkt_set_timestamp(resent, (uint32_t) timestamp_start);
            size_t len = 16 + pkt_get_length(resent);
            char buf[len];
            pkt_status_code status = pkt_encode(resent, buf, &len);
            if (status != PKT_OK){
                ERROR("encode failed");
            }
            write(sock, buf, len);
            data_sent++;
            packet_retransmitted++;
        } 
        FD_SET(sock, &rfds);
        FD_SET(fd, &rfds);
        select(max(sock,fd) + 1, &rfds, NULL, NULL, NULL);
        if (FD_ISSET(sock, &rfds)){
            char buf[10]; 
            ssize_t ss = read(sock, buf, 10); 
            size_t len = (size_t)ss;
            pkt_t *pkt = pkt_new();
            pkt_status_code status = pkt_decode(buf, len, pkt);

            if (status != PKT_OK || !ack_check(win, pkt_get_seqnum(pkt))){
                packet_ignored++;
            }
            else{
                timestamp_end = clock();
                max_rtt = max((uint32_t)timestamp_end - pkt_get_timestamp(pkt), max_rtt);
                min_rtt = min((uint32_t)timestamp_end - pkt_get_timestamp(pkt), min_rtt);
                if(pkt_get_type(pkt) == 2){
                    ack_received ++;
                    last_ack = pkt_get_seqnum(pkt);
                    if(pkt_get_seqnum(pkt) == last_pkt){
                        break;
                    }
                    if(pkt_get_seqnum(pkt) != seqnum){
                        pkt_t * resent = get(win , pkt_get_seqnum(pkt));
                        timestamp_start = clock();
                        pkt_set_timestamp(resent,(uint32_t) timestamp_start);
                        size_t len = 16 + pkt_get_length(resent);
                        char buf[len];
                        pkt_status_code status = pkt_encode(resent, buf, &len);
                        if (status != PKT_OK){
                            ERROR("encode failed: %d", status);
                        }
                        write(sock, buf, len);
                        data_sent++;
                        packet_retransmitted++;
                        

                    }
                    int dif = modulo(win->start, pkt_get_seqnum(pkt));
                    for (int i = 0; i < dif; i++){
                        pop(win);
                    }
                }
                if(pkt_get_type(pkt) == 3){
                    nack_received ++;
                    pkt_t * resent = get(win , pkt_get_seqnum(pkt));
                    timestamp_start = clock();
                    pkt_set_timestamp(resent, (uint32_t)timestamp_start);
                    size_t len = 16 + pkt_get_length(resent);
                    char buf[len];
                    pkt_status_code status = pkt_encode(resent, buf, &len);
                    if (status != PKT_OK){
                        ERROR("encode failed: %d", status);
                    }
                    write(sock, buf, len);
                    data_sent++;
                    packet_retransmitted++;
                }
            }
        }
        if (FD_ISSET(fd, &rfds)  && modulo(win->start, win->end) < 30 && last_pkt == -2){
            char payload[512];
            ssize_t ss = read(fd, payload, 512);

            if (ss <= 0){
                pkt_t * pkt = pkt_new();
                pkt_set_type(pkt, type);
                pkt_set_tr(pkt, tr);
                pkt_set_window(pkt, window);
                pkt_set_seqnum(pkt, seqnum);
                pkt_set_length(pkt, 0);
                timestamp_start = clock();
                pkt_set_timestamp(pkt, (uint32_t)timestamp_start);
                pkt_set_crc1(pkt, crc1);
                size_t len = 12;
                push(win,pkt);
                char buf[len];
                pkt_status_code status = pkt_encode(pkt, buf, &len);
                if (status != PKT_OK){
                    ERROR("encode failed: %d", status);
                }
                write(sock, buf, len);
                data_sent += 1;
                seqnum = (seqnum + 1) % 256;
                last_pkt = seqnum;
            }
            else{
                size_t s = (size_t) ss;
                uint16_t length = (uint16_t)ss;
                pkt_t * pkt = pkt_new();
                pkt_set_type(pkt, type);
                pkt_set_tr(pkt, tr);
                pkt_set_window(pkt, window);
                pkt_set_seqnum(pkt, seqnum);
                pkt_set_length(pkt, length);
                timestamp_start = clock();
                pkt_set_timestamp(pkt, (uint32_t)timestamp_start);
                pkt_set_crc1(pkt, crc1);
                pkt_set_crc2(pkt, crc2);
                pkt_set_payload(pkt, payload, length);
                size_t len = 16 + s;
                push(win,pkt);
                char buf[len];
                pkt_status_code status = pkt_encode(pkt, buf, &len);
                if (status != PKT_OK){
                    ERROR("encode failed: %d", status);
                }
                write(sock, buf, len);
                data_sent++;
                seqnum = (seqnum + 1) % 256;
            }
        }
    }

    FILE * fd2 = stdout;
    if(stats_filename != NULL){
        fd2 = fopen(stats_filename, "w");
    }

    fprintf(fd2,"\ndata_sent:%d\ndata_received:0\ndata_truncated_received:0\nack_sent:0\nack_received:%d\nnack_sent:0\nnack_received:%d\npacket_ignored:%d\nmin_rtt:%d\nmax_rtt:%d\npacket_retransmitted:%d\n",data_sent, ack_received, nack_received, packet_ignored,min_rtt,max_rtt, packet_retransmitted);
    return EXIT_SUCCESS;
}