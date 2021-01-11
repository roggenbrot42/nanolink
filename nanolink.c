#include "stdlib.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "nanolink.h"
#include "types.h"
#include "ext_hdr.h"
#include "list.h"
#include "tctrl.h"

#ifdef __C3000__
extern void termaus(char *buffer);
#else
#define termaus(X) printf("%s\n", X);
#endif

static int _init_vc (struct vc_state * vc){
    vc->lr = 255; //Last valid in-order received
    vc->rr = 255;    

    vc->ls = 255;
    vc->nsn = 0; // ls + 1;
    vc->vs = 255;
    vc->retransmit = 0;

    vc->send_stat = 0;
    vc->need_stat = 0;

    vc->deficit = 0;

    INIT_QUEUE(&vc->sendq);
    INIT_QUEUE(&vc->sentq);
    INIT_QUEUE(&vc->rcvq);

    return 1;
}

static int nanolink_rcvq_del(struct vc_state * vc, struct arq_element * elem){
    queue_del(&elem->list,&vc->rcvq);
    free(elem);
    return 1;
}

static int _reset_vc(struct nanolink_state * inst, struct vc_state * vc){
    struct arq_element * ae;
    struct list_head * pos,*tmp;

    queue_splice(&vc->sendq, inst->cleanup_list);
    queue_splice(&vc->sentq, inst->cleanup_list);
    //queue_splice(&vc->narqq, inst->cleanup_list);


    list_for_each_safe(pos,tmp, &vc->rcvq.list){
        ae = list_entry(pos, struct arq_element, list);
        nanolink_rcvq_del(vc,ae);
    }

    return 1;
}

int nanolink_init(struct nanolink_state * inst,
        uint8_t ws, uint8_t wr,
        struct queue * cl, uint16_t * quanti){

    int i;

    inst->ws = ws;
    inst->wr = wr;

    inst->cleanup_list = cl;

    for(i = 0; i< NUM_CHAN; i++){
        _init_vc(&inst->vc_stats[i]);
        inst->vc_stats[i].quantum = quanti[i];
    }

    inst->cstate = ST_CONNDOWN;
    inst->send_pong = 0;
    inst->ping_id = 42;
    inst->missed_frames = 0;

    return 1;
}

static int _reset_nanolink(struct nanolink_state * inst){
    int i;

    for(i = 0; i < NUM_CHAN;i++){
        _reset_vc(inst,&inst->vc_stats[i]);
        _init_vc(&inst->vc_stats[i]);
    }

    inst->missed_frames = 0;

    return 1;
}

int nanolink_enqueue(struct nanolink_state * inst, struct nanolink_frame * fr){
    int vcid = fr->header.vc;
    struct vc_state * vc = &inst->vc_stats[vcid];

    fr->header.f_ext = 0;
    if(!nanolink_connected(inst))fr->header.f_arq = 0;

    if(fr->header.f_arq == 1){
        fr->header.seqn = inst->vc_stats[fr->header.vc].nsn;
        inst->vc_stats[fr->header.vc].nsn++;
        queue_add(&fr->list, &vc->sendq);
    }else{
        queue_prepend(&fr->list, &vc->sendq);
    }

    return 1;
}

int nanolink_cstate_handler(struct nanolink_state * inst, enum cstate_event id){

    switch(inst->cstate){
        case ST_CONNDOWN:
            if(id == EV_RSYN){
                //we have a connection est. request
                inst->cstate = ST_RSYN;
            }
            else if(id == EV_RCC){
                inst->cstate = ST_TEARDOWN;
            }
            else if(id == EV_ARQ || id == EV_RSYNACK){
                inst->cstate = ST_CCWAIT;
            }
            else{
                //ignore
            }
            break;
        case ST_RSYN:
            if(id == EV_RSYN || id == EV_SSYNACK){
                //Do nothing, wait
            }
            else if(id == EV_ARQ){
                //connection established
                inst->cstate = ST_CONNUP;
            }
            else if(id == EV_RCC || id == EV_RCCACK){
                inst->cstate = ST_TEARDOWN;
                _reset_nanolink(inst);
            }
            else if(id == EV_TIMEOUT){
                inst->cstate = ST_CONNDOWN;
            }
            else{
                //protocol error
            }
            break;
        case ST_CONNUP:
            if(id == EV_ARQ){
                //Do nothing
                //maybe reset timeout
            }
            if(id == EV_RCC){
                //Conn teardown request
                inst->cstate = ST_TEARDOWN;
                _reset_nanolink(inst);
            }
            else if(id == EV_RSYNACK || id == EV_RCCACK || id == EV_TIMEOUT){
                //protocol error
                inst->cstate = ST_CONNDOWN;
                _reset_nanolink(inst);
            }
            else if(id == EV_RSYN){
                inst->cstate = ST_RSYN;
                _reset_nanolink(inst);
            }
            else{
                //protocol error
            }
            break;
        case ST_TEARDOWN:
            if(id == EV_SCCACK){
                inst->cstate = ST_CONNDOWN;
            }
            else{

            }
            break;
        case ST_CCWAIT:
            if(id == EV_SCC){
                //do nothing
            }
            else if(id == EV_RCCACK){
                inst->cstate = ST_CONNDOWN;
            }
            else if(id == EV_RCC){
                inst->cstate = ST_TEARDOWN;
            }
            else if(id == EV_TIMEOUT){
                inst->cstate = ST_CONNDOWN;
            }
            else{
                //Do nothing
            }
            break;
        default: ;
                 //state machine error?
    }

    return 1;
}

inline uint8_t nanolink_connected(struct nanolink_state * inst){
    return (inst->cstate == ST_CONNUP)?1:0;
}

/*
 * Pack Header of nanolink frame
 * returns pointer to begin of payload memory
 */
octet * nanolink_pack_head(struct nanolink_frame * fr, octet * buff){

    *buff = fr->header.seqn;
    buff++;
    *buff = (fr->header.f_arq << 7) | (fr->header.f_ext << 6); //ARQ flag
    *buff = *buff | (fr->header.len >> 4); //upper length field
    buff++;
    *buff = (fr->header.len << 4) | (fr->header.vc << 1); //lower length field + VC
    buff++;

    return buff;
}

struct nanolink_frame * nanolink_unpack(octet * buff, struct nanolink_frame * fr){
    fr->header.seqn = *buff++;
    fr->header.f_arq = (*buff & 0x80) >> 7;
    fr->header.f_ext = (*buff & 0x40) >> 6;
    fr->header.len = (int)(*buff & 0x3F) << 4;
    buff++;
    fr->header.len += *buff >> 4;
    fr->header.vc = (*buff & 0x0F) >> 1;

    return fr;
}

static int _gre(uint8_t a, uint8_t b){
    return (_cmp_mod(a,b) >= 0)?1:0;
}

inline int _cmp_mod(uint8_t a, uint8_t b){
    uint8_t d = a - b;

    if(d <= 127 && d >= 1) //b<a
        return -1;
    else if (d >= 128) //b>a
        return 1;
    else
        return 0;
}

int _within_bounds(uint8_t lo, uint8_t up, uint8_t sn){
    if(_cmp_mod(lo, sn) > 0){
        if(_cmp_mod(sn, up) >= 0){
            return 1;
        }
    }
    return 0;
}


int _nanolink_stat_unpack(octet * extp, struct stat_hdr *  stat){
    stat->fields = *extp;
    extp++;
    stat->len = *extp;
    extp++;
    stat->lr = *extp;
    extp++;
    stat->rr = *extp;
    extp++;
    if(stat->len != 0){
        stat->entries = extp;
    }else{
        stat->entries = 0;
    }

    return 1;
}

int _handle_stat(struct nanolink_state *inst, octet * extp, int vcid){
    struct stat_hdr stat_elem, *stat;
    struct vc_state * vc;
    struct list_head * pos,*pos2;
    struct nanolink_frame * elem;
    uint8_t * sptr;
    int len;
    int cmp;

    if(inst->cstate != ST_CONNUP){
        return 0;
    }

    stat = &stat_elem;

    _nanolink_stat_unpack(extp,stat);

    if(stat->len > inst->ws){
        return 0;
    }
    if(_cmp_mod(stat->rr,stat->lr)>0){
        return 0;
    }

    sptr = stat->entries;
    vc = &inst->vc_stats[vcid];

    if(_cmp_mod(vc->ls,stat->lr)<0){
        return 0;
    }


    vc->ls = stat->lr;
    vc->vs = stat->rr;
    //TODO: fast forward ls

    len = stat->len - 4;
    if(len > 0){ 
        vc->retransmit = 1;
    }else{
        vc->retransmit = 0;
    }
    vc->rs = stat->lr;

    //Delete all up to including L(R)
    list_for_each_safe(pos, pos2, &vc->sentq.list){
        elem = list_entry(pos, struct nanolink_frame, list);
        if(_cmp_mod(elem->header.seqn, stat->lr) >= 0){
            queue_del(&elem->list, &vc->sentq);
            queue_add(&elem->list, inst->cleanup_list);
        }
    }
    //Delete non-missing between L(R) and R(R)
    list_for_each_safe(pos, pos2, &vc->sentq.list){
        elem = list_entry(pos, struct nanolink_frame, list);
        if(len > 0){
            cmp = _cmp_mod(elem->header.seqn, *sptr);
            if(cmp > 0){ //smaller than, thus must been received
                queue_del(&elem->list, &vc->sentq);
                queue_add(&elem->list, inst->cleanup_list);
            }
            else if(cmp == 0){
                sptr++;
                len--;
            }               
        }
        else{ //handle R(R)
            cmp = _cmp_mod(elem->header.seqn, stat->rr);
            if(cmp >= 0){
                queue_del(&elem->list, &vc->sentq);
                queue_add(&elem->list, inst->cleanup_list);
            }
        }
    }

    vc->need_stat = 0;

    return 1;
}

int _handle_poll(struct nanolink_state *inst, int vc){
    if(inst->cstate == ST_CONNUP){
        inst->vc_stats[vc].send_stat = 1;
    }

    return 1;
}

int _handle_ping(struct nanolink_state * inst, octet * buff){
    inst->send_pong = 1;
    inst->ping_id = *(++buff);
    return 1;
}

static int _check_exthdr(struct nanolink_state * inst, octet * buff, size_t len, int vc){
    int nxt = 0;
    enum ext_id exid;
    uint8_t offset=0;
    unsigned int tot_len=0;

    do{ 
        nxt = 0x1 & *buff;
        exid = (0xFE & *buff) >> 1;
        switch(exid){
            case STAT:
                _handle_stat(inst,buff,vc);
                offset = *(buff+1);
                break;
            case POLL:
                _handle_poll(inst,vc);
                offset = 1;
                break;
            case SYN:
                nanolink_cstate_handler(inst, EV_RSYN);
                offset = 1;
                break;
            case SYNACK:
                nanolink_cstate_handler(inst, EV_RSYNACK);
                offset = 1;
                break;
            case CC:
                nanolink_cstate_handler(inst, EV_RCC);
                offset = 1;
                break;
            case CCACK:
                nanolink_cstate_handler(inst, EV_RCCACK);
                offset = 1;
                break;
            case PING:
                offset = 2;
                _handle_ping(inst,buff);
                break;
            case PONG:
                offset = 1;
                break;
            case CTRLW:
                //handler removed from this code
                break;
            default:
                return tot_len;
        }
        buff += offset;
        tot_len += offset;
    }while(nxt == 1 && tot_len <= len-2);


    return tot_len;
}



int nanolink_generate_poll(octet ** buffer,uint8_t next){
    octet * buff = *buffer;
    enum ext_id exid = POLL;

    *buff = exid << 1 | (0x1 & next);
    buff++;
    *buffer = buff;

    return 1;
}

int nanolink_stat_size(struct vc_state * vc){
    return vc->rcvq.len+4;
}

static octet * nanolink_insert_exthdr(struct nanolink_state * inst, struct nanolink_frame * fr,
        octet * dest, int * extlen){
    struct vc_state * vc;
    int vcid;
    int appendstat = 0;
    int appendpoll = 0;
    vcid = fr->header.vc;
    vc = &inst->vc_stats[vcid];

    if(vc->send_stat){
        if(MAX_FRAME_SIZE-1 >= nanolink_stat_size(vc) + fr->header.len){
            fr->header.f_ext = 1;
            appendstat = 1;
            vc->send_stat = 0;
            *extlen += nanolink_stat_size(vc);
            fr->header.len += *extlen;
        }
    }
    if(vc->need_stat){
        if(MAX_FRAME_SIZE-1 > fr->header.len){
            fr->header.len += 1;
            fr->header.f_ext = 1;
            appendpoll = 1;
            *extlen += 1;
        }
    }

    if(appendstat){
        nanolink_generate_stat(vc,&dest,(uint8_t)appendpoll);
    }
    if(appendpoll){
        nanolink_generate_poll(&dest,0);
    }

    return dest;
}

int nanolink_need_immediate(struct nanolink_state * inst){
    return (inst->send_pong ||
            inst->cstate == ST_RSYN||
            inst->cstate == ST_TEARDOWN||
            inst->cstate == ST_CCWAIT );
}

int nanolink_build_immediate(struct nanolink_state * inst, octet * dest){
    uint8_t id;
    uint8_t ret = 1;
    if(inst->cstate == ST_RSYN){
        id = SYNACK;
        nanolink_cstate_handler(inst,EV_SSYNACK);
    }
    else if(inst->cstate == ST_TEARDOWN){
        id = CCACK;
        nanolink_cstate_handler(inst,EV_SCCACK);
    }
    else if(inst->cstate == ST_CCWAIT){
        id = CC;
        nanolink_cstate_handler(inst,EV_SCC);
    }
    else {
        id = PONG;
        inst->send_pong = 0;
        dest[1] = inst->ping_id;
        ret = 2;
    }
    dest[0] = id << 1;
    return ret;
}

int nanolink_send(struct nanolink_state * inst, struct nanolink_frame * fri, octet * dest){
    struct vc_state * vc;
    int vcid;
    int extlen = 0;
    octet * buffptr = dest+HEADER_LEN;
    struct nanolink_frame fr;
    uint8_t hw;

    if(fri != 0){ //Real frame?
        fr = *fri;
        vcid = fr.header.vc;
        vc = &inst->vc_stats[vcid];
        uint16_t oldlen = (uint16_t) fr.header.len;
        buffptr = nanolink_insert_exthdr(inst,&fr,buffptr,&extlen);

        if(oldlen > 2){ //only copy if payload present
            memcpy(buffptr,fr.addr+HEADER_LEN,oldlen-HEADER_LEN+1);
        }
        if(fri->header.f_arq == 0){
            queue_move(&fri->list,&vc->sendq,inst->cleanup_list);
        }else if(_gre(fri->header.seqn,vc->vs)){
            inst->vc_stats[vcid].rs = fr.header.seqn;
        }else{
            queue_move(&fri->list,&vc->sendq,&vc->sentq);

            //send stat every ws/2 frames
            hw = (inst->ws >> 2) +1 + vc->ls;
            if(_cmp_mod(hw,fri->header.seqn) > 0){ //TODO: prevent spamming this header
                vc->need_stat = 1;
            }
        }
    }
    else if(nanolink_need_immediate(inst)){
        fr.header.seqn = 0xDD;
        fr.header.f_arq = 0;
        fr.header.f_ext = 1;
        fr.header.vc = 7;

        fr.header.len = HEADER_LEN -1 + nanolink_build_immediate(inst, buffptr);
    }else{
        return 0; //Nothing to send, is okay too
    }
    nanolink_pack_head(&fr, dest);

    return fr.header.len+1;
}

int nanolink_generate_stat(struct vc_state * vc, octet ** buffer,uint8_t next){
    struct list_head * pos;
    struct arq_element * tmp;
    octet * bpr = *buffer;
    enum ext_id id = STAT;

    *bpr = ((octet) id) << 1 | (0x1 & next);
    bpr++;
    *bpr = vc->rcvq.len+4;
    bpr++;
    *bpr = vc->lr;
    bpr++;
    *bpr = vc->rr;
    bpr++;

#ifndef __C3000__
    printf("STAT: lr: %d rr: %d",vc->lr, vc->rr);
#endif
    list_for_each(pos, &vc->rcvq.list){
        tmp = list_entry(pos, struct arq_element, list);
        *bpr = tmp->seqn;
#ifndef __C3000__
        printf(" %d", tmp->seqn);
#endif
        bpr++;
    }
#ifndef __C3000__
    printf("\n");
#endif

    *buffer = bpr;

    return vc->rcvq.len;
}

static inline int _check_hdr(struct nanolink_state * inst, struct nanolink_frame_hdr * hdr){
    if(hdr->f_arq == 0){    //NARQ frame
        return 1;
    }
    //check seq no
    uint8_t lr = inst->vc_stats[hdr->vc].lr;
    if(_within_bounds(lr,lr+inst->wr,hdr->seqn)){
        return 1;
    }
    return 0;
}

static inline int nanolink_rcvq_add(struct vc_state * vc, uint8_t seqn){
    struct arq_element * elem;
    elem = (struct arq_element*)malloc(sizeof(struct arq_element));
    elem->seqn = seqn;
    queue_add(&elem->list, &vc->rcvq);
    return 1;
}



/*
 * TODO: process extension headers directly after rx
 * 
 */
int nanolink_receive(struct nanolink_state * inst, octet * src, octet * dst, struct nanolink_frame * fr){
    struct nanolink_frame_hdr * head;
    struct arq_element * elem,*tmp;
    struct list_head * pos, *pos2;
    struct vc_state * vc;
    uint8_t c=0;
    int ret = 1;
    int totlen = 0;
    uint8_t ncpy = 0;
    struct nanolink_frame dummy;

    if(fr == 0){
        ncpy = 1;
        fr = &dummy;
    }

    nanolink_unpack(src, fr);
    head = &fr->header;
    vc = &inst->vc_stats[head->vc];

    if(head->f_arq == 0){
        goto handle_ext;
    }

    nanolink_cstate_handler(inst, EV_ARQ);

    if(inst->cstate != ST_CONNUP){ //are we not connected now?
        ret = -1; //-1 == free frame after use
        goto out;
    }

    if(!_check_hdr(inst, head)){
        vc->send_stat = 1;
        ret = -1; //-1 == free frame after use
        goto out; //sequence number not valid
    }


    //Check if frame is in sequence
    if(head->seqn == (vc->lr+1)%256){
        vc->lr = (vc->lr+1)%256;
        if(!list_empty(&vc->rcvq.list)){//something missing?
            tmp = list_first_entry(&vc->rcvq.list,struct arq_element, list);
            if(tmp->seqn == head->seqn){ //retransmit of missing frame
                nanolink_rcvq_del(vc,tmp);
                if(!list_empty(&vc->rcvq.list)){
                    elem = list_first_entry(&vc->rcvq.list, struct arq_element, list);
                    vc->lr = elem->seqn-1; //move L(R) to the next missing frame
                }
                else{
                    vc->lr = vc->rr; //L(R) und R(R) in sync, nothing missing
                    vc->send_stat = 1; //inform other node
                }
            }
        }else{//no losses
            vc->rr = vc->lr;
        }
    }
    //Check if there is a gap
    else if(head->seqn == (vc->rr+1)%256){
        vc->rr = (vc->rr+1)%256;
    }
    //Single miss
    else if(head->seqn == (vc->rr+2)%256){ // == R(R)+2
        nanolink_rcvq_add(vc, vc->rr+1);
        vc->rr = head->seqn;
        inst->missed_frames++;
    }
    //Multimiss
    else if(_cmp_mod(vc->rr, head->seqn) == 1){ // > R(R)
        for(c=vc->rr+1;_cmp_mod(c,head->seqn) == 1;c++){
            nanolink_rcvq_add(vc,c);
        }
        vc->rr = head->seqn;
        inst->missed_frames += (head->seqn - vc->rr)%256;
    }
    else // <= R(R), must be retransmission
    {   
        uint8_t found = 0;
        elem = list_first_entry(&vc->rcvq.list, struct arq_element, list);
        if(head->seqn == elem->seqn){
            nanolink_rcvq_del(vc,elem);
            found = 1;
        }
        else{
            list_for_each_safe(pos,pos2, &vc->rcvq.list){
                elem = list_entry(pos, struct arq_element, list);
                if(elem->seqn == head->seqn){ //missing
                    nanolink_rcvq_del(vc,elem);
                    found = 1;
                    break;
                }
            }
        }
        if(found == 0){
            vc->send_stat = 1; //sync with peer
            return -1; //free frame after use    //sequence number not valid
        }
    }

    //handle extension header
handle_ext: 

    if(head->f_ext == 1){
        totlen = _check_exthdr(inst, src+HEADER_LEN,fr->header.len,fr->header.vc);
        if(totlen == 0){  //TODO: maybe check more carefully
            return 0;
        }else{
            fr->header.len -= totlen;
        } 
    }

    nanolink_pack_head(fr, src+totlen); //Copy header
    if(fr->header.len > 2 && fr->header.len < MAX_FRAME_SIZE && ncpy == 0){
        memcpy(dst,src+totlen,fr->header.len+1);
    }else{
        ret = -1;
    }
out:

    return ret;
}

