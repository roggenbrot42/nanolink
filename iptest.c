#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>

#include "nanolink.h"
#include "nanolink_scheduler.h"

uint16_t rxport = 9991;
struct in_addr ip;

void print_usage(){
    printf("Nanolink over UDP Test Program\n"
        "----------------------------------\n"
        "Usage: udptest [-r rxport] addr\n"
        "Default Ports:\n"
        "\trx: 9991\n");
}

int parse_args(int argc, char * argv[]){
    if(argc > 5){
         printf("Too many arguments!\n");
    } 
    if (argc == 2){
        if(inet_aton(argv[1],&ip)){
            return 1;
        }
    }
    if (argc == 4){
        if(argv[1][1] == 'r'){
            rxport = atoi(argv[2]);
        }
        else{
            return 0;
        }
        if(!inet_aton(argv[3],&ip)) return 0;
        return 1;
    }else{
        printf("Not enough arguments\n");
    }
    return 0;
}    
        
        

int main(int argc, char * argv[]){

    if(!parse_args(argc, argv)){
        print_usage();
        return 0;
    }

    printf("Starting Nanolink tester...\n"
        "Addr: %s\n"
        "Port: %d\n",inet_ntoa(ip),rxport);

    struct sockaddr_in tx_sock,rx_sock;
    socklen_t addrlen;
    int sfd;
    int ret;
    const int y = 1;

    sfd = socket(AF_INET, SOCK_DGRAM,IPPROTO_UDP);
    if(sfd < 0){
        printf("Can't open socket: %s\n", strerror(errno));
        return 0;
    }

    tx_sock.sin_family = AF_INET;
    tx_sock.sin_addr.s_addr = ip.s_addr;
    tx_sock.sin_port = htons(rxport);
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(int));
    ret = bind ( sfd, (struct sock_addr *) &tx_sock, sizeof(tx_sock));

    if(ret < 0){
        printf("Error binding socket: %s\n", strerror(errno));
        return 0;
    }
    //SOCKET BOUND, INIT NANOLINK
    printf("Waiting for Client...\n");

    struct nanolink_state st,*inst;
    struct queue cl;
    INIT_QUEUE(&cl);

    inst = &st;

    uint16_t quantum[NUM_CHAN] = {410,1230,410,820,1230,1230,1,1};
    nanolink_init(inst, 8, 8, 0, &cl,quantum); 

    char * buffer, *frame_addr;
    struct nanolink_frame fr,*txf;
    int len,n;
    
    fr.addr = (dma_addr) calloc(MAX_FRAME_SIZE,sizeof(char));
    buffer = (char *) calloc (MAX_FRAME_SIZE+5,sizeof(char));

    struct nanolink_frame frames[64];
    for(int i=0;i<64;i++){
        frames[i].addr = (dma_addr) calloc(MAX_FRAME_SIZE,sizeof(char));
        queue_add(&frames[i].list, &cl);
    }

    while(1){
        len = sizeof(rx_sock);
        n = recvfrom(sfd, buffer, MAX_FRAME_SIZE+5, 0, (struct sock_addr *) &rx_sock, &len);

        printf("Rcv len %d\n",n);
        if(n<0){
            printf("Error %s\n",strerror(errno));
        }
 
        if(nanolink_receive(inst,buffer+3,fr.addr,&fr)<0){
            printf("Rcv error\n");
            memset(fr.addr, 0 , MAX_FRAME_SIZE);
        }else{
            printf("Rcv len %d vc %d arq %d %s\n",fr.header.len, fr.header.vc, fr.header.f_arq, fr.addr+3);
        }

        memset(buffer, 0 , MAX_FRAME_SIZE+5);
        buffer[0] = 0xFA;
        buffer[1] = 0xF3;
        buffer[2] = 0x20;

        txf = nanolink_get_next(inst);
        if(nanolink_send(inst,txf,inst->sp)){
            memcpy(buffer+3,inst->sp,MAX_FRAME_SIZE);
            n = sendto(sfd, buffer, MAX_FRAME_SIZE+5,0,(struct sock_addr*) &rx_sock, len);
            if(n < 0){
                printf("Send Error %s\n", strerror(errno));
            }else{
                printf("Send len %d %x\n",n,*(buffer+6));
            }
        }else{
            printf("Nanolink send error\n");
        }
     
        memset(buffer, 0 , MAX_FRAME_SIZE+5);
    }
   
    free(buffer);
    free(fr.addr); 

    return 1;
} 
