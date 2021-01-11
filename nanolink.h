#ifndef NANOLINK_H
#define NANOLINK_H

#include "types.h"
#include "list.h"
#include "ext_hdr.h"

#define MAX_WINDOW 127
#define NUM_CHAN 8
#define TM_CHAN 7

#define ERR_HEAD -1
#define ERR_BUFF -3
#define ERR_EXTH -4

#define MAX_PAYLOAD     1021
#define MAX_EXT_DATA    256
#define HEADER_LEN      3

#define SET_BIT(val, bitIndex) val |= (1 << bitIndex)
#define CLEAR_BIT(val, bitIndex) val &= ~(1 << bitIndex)
#define TOGGLE_BIT(val, bitIndex) val ^= (1 << bitIndex)
#define BIT_IS_SET(val, bitIndex) (val & (1 << bitIndex))

struct nanolink_frame_hdr {
    octet seqn;
    unsigned int f_arq,
                 f_ext,
                 len,
                 vc;
};

struct arq_element{
    octet seqn;
    struct list_head list;
};

struct nanolink_frame {
    struct nanolink_frame_hdr header;
    /* internal stuff */
    struct list_head list; //list f. snd,rcv queues
};

enum con_state {
    ST_CONNDOWN, //Idle, not Connected
    ST_RSYN,  //Received SYN
    ST_CONNUP, //Connection is up
    ST_TEARDOWN, //Received CC
    ST_CCWAIT //Received ARQ or SYNACK in Idle
};

enum cstate_event{
    EV_RSYN,
    EV_RSYNACK,
    EV_SSYNACK,
    EV_SCC,
    EV_RCC,
    EV_SCCACK,
    EV_RCCACK,
    EV_ARQ,
    EV_TIMEOUT
};

enum ext_id {
    STAT,
    POLL,
    SYN,
    SYNACK,
    CC,
    CCACK,
    PING,
    PONG,
    CTRLW
};


struct vc_state {
    //Status Variables
    uint8_t lr; //L(R) last in order seqn
    uint8_t rr; //R(R) highest correctly received seqn   

    uint8_t ls; //L(S) send window start
    uint8_t nsn; //N(S) next in-sequence number
    uint8_t vs; //V(S) highest acknowledged sequence number
    uint8_t rs; //R(S) Last retransmitted number (for counting on retransmit)
    uint8_t retransmit; //Boolean

    uint8_t need_stat;
    //char requested_stat;
    uint8_t send_stat;


    struct queue sendq;
    struct queue sentq; //struct nanolink_frame

    struct queue rcvq; //struct arq_element

    /* For deficit rount robin in future */
    uint16_t deficit;
    uint16_t quantum;
};

struct nanolink_state {
    struct vc_state vc_stats[NUM_CHAN];

    octet ws; //Window size send max 127
    octet wr; //Window size receive

    struct queue * cleanup_list;

    enum con_state cstate;
    uint8_t send_pong;
    uint8_t ping_id;
    
    uint32_t missed_frames;
    
    octet sp[MAX_FRAME_SIZE]; //scratchpad
};

struct nanolink_exthdr {
    union {
        struct  ext_fields {
            unsigned extid: 7,
                     nxthdr:1;
        }fields;
        octet raw;
    };
};


int nanolink_init(struct nanolink_state*, uint8_t, uint8_t, struct queue *, uint16_t *);
struct nanolink_frame * nanolink_get_dummy(struct nanolink_state *, uint8_t);
int nanolink_enqueue(struct nanolink_state *, struct nanolink_frame *);
uint8_t nanolink_connected(struct nanolink_state*);
int nanolink_receive(struct nanolink_state *,octet *, octet*,struct nanolink_frame*);
octet * nanolink_pack_head(struct nanolink_frame * , octet * );
int nanolink_send(struct nanolink_state *, struct nanolink_frame *, octet *);
struct nanolink_frame * nanolink_unpack(octet * , struct nanolink_frame *);
int _cmp_mod(uint8_t, uint8_t);
int _within_bounds(uint8_t, uint8_t, uint8_t);
int _handle_stat(struct nanolink_state *, octet *, int);
int nanolink_generate_stat(struct vc_state *, octet**,uint8_t);
int nanolink_cstate_handler(struct nanolink_state *, enum cstate_event);
int nanolink_need_immediate(struct nanolink_state *);
int nanolink_stat_size(struct vc_state *);
#endif
