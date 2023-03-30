#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "log.h"
#include <sys/select.h>
#include "packet_implem.c"
#include "packet_interface.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

int print_usage(char *prog_name) {
    ERROR("Usage:\n\t%s [-s stats_filename] listen_ip listen_port", prog_name);
    return EXIT_FAILURE;
}

int min(int a, int b){
    if(a < b) return a;
    return b;
}

int modulo(int a, int b){
    int diff = (b-a) % 256;
    if(diff < 0){
        return 256 + diff;
    }
    return diff;
}

void updateWin(pkt_t** win, int start){
    for (int i = start; i < 31; i++){
        if(win[i-start] != NULL){
            pkt_del(win[i-start]);
            win[i-start] = NULL;
            if(win[i] != NULL){
                win[i-start] = pkt_new();
                memcpy(win[i-start], win[i], sizeof(pkt_t));
                pkt_set_payload(win[i-start], pkt_get_payload(win[i]), pkt_get_length(win[i]));
            }
        }
    }
    for (int j = 31-start; j < 31; j++){
        if(win[j] != NULL){
            pkt_del(win[j]);
            win[j] = NULL;
        }
    }
}

int main(int argc, char **argv) {
    int opt;
    char *filename = NULL;
    char *stats_filename = NULL;
    char *listen_ip = NULL;
    char *listen_port_err;
    uint16_t listen_port;

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

    listen_ip = argv[optind];
    listen_port = (uint16_t) strtol(argv[optind + 1], &listen_port_err, 10);
    if (*listen_port_err != '\0') {
        ERROR("Receiver port parameter is not a number");
        return print_usage(argv[0]);
    }
    ASSERT(1 == 1);
    DEBUG_DUMP("Some bytes", 11);

    ERROR("Receiver has following arguments: stats_filename is %s, listen_ip is %s, listen_port is %u",
        stats_filename, listen_ip, listen_port);

    DEBUG("You can only see me if %s", "you built me using `make debug`");
    ERROR("This is not an error, %s", "now let's code!");


    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if(sock < 0){
        return -1;
    }
    struct sockaddr_in6 listen_addr;
    bzero(&listen_addr, sizeof (struct sockaddr_in6));
    listen_addr.sin6_family = AF_INET6;
    listen_addr.sin6_port = htons(listen_port);
    if (inet_pton(AF_INET6, listen_ip, &listen_addr.sin6_addr) != 1) {
         return -1;
    }
    if (bind(sock, (struct sockaddr *) &listen_addr, sizeof (struct sockaddr_in6)) != 0) {
        return -1;
    }
    char buf[32];
    struct sockaddr_in6 *src_addr;
    socklen_t addrlen = sizeof(struct sockaddr_in6);
    if(recvfrom(sock, buf, 32, MSG_PEEK, (struct sockaddr *)&src_addr, (socklen_t *)&addrlen) < 0){
        return -1;
    }
    if(connect(sock, (struct sockaddr *)&src_addr, addrlen) != 0){
        return -1;
    }
    int data_received = 0;
    int data_truncated_received = 0;
    int ack_sent = 0;
    int nack_sent = 0;
    int packet_ignored = 0;
    int packet_duplicated = 0;
    int window = 31;
    pkt_t* win[31];
    for(int i = 0; i < 31; i++){
        win[i] = NULL;
    }
    int seq = 0;
    fd_set rfds;
    FD_ZERO(&rfds);
    int fd = STDOUT_FILENO;
    if(filename != NULL) fd = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0644);


    while (1 == 1){
        FD_SET(sock, &rfds);
        select(sock + 1, &rfds, NULL, NULL, NULL);
        if (FD_ISSET(sock, &rfds)){
            char buf[528];
            ssize_t ss = read(sock, buf, 528);
            size_t len = (size_t)ss;
            pkt_t *pkt = pkt_new();
            pkt_status_code status = pkt_decode(buf, len, pkt);
            if (status != PKT_OK){
                ERROR("PKT NOT OK: %d", status);
                packet_ignored++;
            }

            else{
                
                data_received++;
                if(pkt_get_length(pkt) == 0){
                    if(pkt_get_seqnum(pkt) == seq){
                        pkt_t *ack = pkt_new();
                        pkt_set_type(ack, PTYPE_ACK);
                        pkt_set_tr(ack, 0);
                        pkt_set_window(ack, window);
                        pkt_set_seqnum(ack, seq+1);
                        pkt_set_timestamp(ack, pkt_get_timestamp(pkt));
                        pkt_set_crc1(ack, 0);
                        char buf[10];
                        size_t l = 10;
                        pkt_status_code status = pkt_encode(ack, buf, &l);
                        if (status != PKT_OK){
                            ERROR("encode failed");
                        }
                        write(sock, buf, 10);
                        pkt_del(ack);
                        pkt_del(pkt);
                        ack_sent++;
                        break;
                    }
                    else{
                        pkt_t *ack = pkt_new();
                        pkt_set_type(ack, PTYPE_ACK);
                        pkt_set_tr(ack, 0);
                        pkt_set_window(ack, window);
                        pkt_set_seqnum(ack, seq);
                        pkt_set_timestamp(ack, pkt_get_timestamp(pkt));
                        pkt_set_crc1(ack, 0);
                        char buf[10];
                        size_t l = 10;
                        pkt_status_code status = pkt_encode(ack, buf, &l);
                        if (status != PKT_OK){
                            ERROR("encode failed");
                        }
                        write(sock, buf, 10);
                        pkt_del(ack);
                        ack_sent++;
                    }
                }
                else{
                    
                    if(modulo(seq, pkt_get_seqnum(pkt)) < 31){
                        if (pkt_get_tr(pkt) == 0){
                            if (pkt_get_seqnum(pkt) == seq){
                                if(window == 31){
                                    write(fd, pkt_get_payload(pkt), pkt_get_length(pkt));
                                    fsync(fd);
                                    seq = (seq + 1) % 256;
                                }
                                else{
                                    win[0] = pkt_new();
                                    memcpy(win[0], pkt, sizeof(pkt_t));
                                    pkt_set_payload(win[0], pkt_get_payload(pkt), pkt_get_length(pkt));
                                    window = window - 1;
                                    for (int i = 0; i < 31; i++){
                                        if (win[i]==NULL){
                                            updateWin(win,i);
                                            break;
                                        }
                                        write(fd, pkt_get_payload(win[i]), pkt_get_length(win[i]));
                                        fsync(fd);
                                        seq = (pkt_get_seqnum(win[i]) + 1) % 256;
                                        window = window + 1;
                                    }
                                }
                            }
                            else{
                                int index = (pkt_get_seqnum(pkt) - seq) ;
                                if(index < 0){
                                    index += 256;
                                }
                                if(win[index] != NULL){
                                    packet_duplicated++;
                                }
                                else{
                                    win[index] = pkt_new();
                                    memcpy(win[index], pkt, sizeof(pkt_t));
                                    pkt_set_payload(win[index], pkt_get_payload(pkt), pkt_get_length(pkt));
                                    window = window - 1;
                                }
                            }
                            pkt_t *ack = pkt_new();
                            pkt_set_type(ack, PTYPE_ACK);
                            pkt_set_tr(ack, 0);
                            pkt_set_window(ack, window);
                            pkt_set_seqnum(ack, seq);
                            pkt_set_timestamp(ack, pkt_get_timestamp(pkt));
                            pkt_set_crc1(ack, 0);
                            char buf[10];
                            size_t l = 10;
                            pkt_status_code status = pkt_encode(ack, buf, &l);
                            if (status != PKT_OK){
                                ERROR("encode failed");
                            }
                            write(sock, buf, 10);
                            pkt_del(ack);
                            ack_sent++;

                        }
                        else{
                            data_truncated_received++;
                            pkt_t *nack = pkt_new();
                            pkt_set_type(nack, PTYPE_NACK);
                            pkt_set_tr(nack, 0);
                            pkt_set_window(nack, window);
                            pkt_set_seqnum(nack, pkt_get_seqnum(pkt));
                            pkt_set_timestamp(nack, pkt_get_timestamp(pkt));
                            pkt_set_crc1(nack, 0);
                            char buf[10];
                            size_t l = 10;
                            pkt_status_code status = pkt_encode(nack, buf, &l);
                            if (status != PKT_OK){
                                ERROR("encode failed");
                            }
                            write(sock, buf, 10);
                            pkt_del(nack);
                            nack_sent++;
                        }
                    }
                    else packet_ignored++;
                }
            }
            pkt_del(pkt);
        }
    }
    FILE * fd2 = stdout;
    if(stats_filename != NULL){
        fd2 = fopen(stats_filename, "w");
    }
    fprintf(fd2,"\ndata_sent:0\ndata_received:%d\ndata_truncated_received:%d\nack_sent:%d\nack_received:0\nnack_sent:%d\nnack_received:0\npacket_ignored:%d\npacket_duplicated:%d\n",data_received,data_truncated_received,ack_sent,nack_sent,packet_ignored,packet_duplicated);
    return EXIT_SUCCESS;            
}

