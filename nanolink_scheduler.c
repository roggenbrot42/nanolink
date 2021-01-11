#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "nanolink_scheduler.h"
#include "nanolink.h"
#include "types.h"
#include "list.h"

#ifdef __C3000__
extern void termaus(char *buffer);
#else
#define termaus(X) printf("%s\n", X);
#endif


struct nanolink_frame * nanolink_select_sent(struct nanolink_state * inst, uint8_t vcid){
    struct nanolink_frame * fr;
    struct list_head * pos;


    list_for_each(pos,&inst->vc_stats[vcid].sentq.list){//Iterate over list of sent frames
        fr = list_entry(pos, struct nanolink_frame, list);
        if(_cmp_mod(fr->header.seqn, inst->vc_stats[vcid].rs) >= 0){ //Skip already retransmitted
            continue; //continue bc not included in STAT message
        }
        else if(_cmp_mod(fr->header.seqn,inst->vc_stats[vcid].vs) == 1){ //select not retransmitted
            return fr;
        }
        else{
            break;
        }
    }

    inst->vc_stats[vcid].retransmit = 0;
    inst->vc_stats[vcid].rs = inst->vc_stats[vcid].ls;

    return 0;
}

struct nanolink_frame * nanolink_select_new(struct nanolink_state * inst, uint8_t vcid){
    struct nanolink_frame * fr = 0;
    struct vc_state * vc;
    vc = &inst->vc_stats[vcid];

    if(!list_empty(&vc->sendq.list)){
        fr = list_first_entry(&vc->sendq.list, struct nanolink_frame, list);
        if(fr->header.f_arq != 0){      
            if(_cmp_mod(vc->ls + inst->ws,fr->header.seqn) < 1){ //seq. avail in window //TODO: check bounds
                //TODO: Mit TIO Buffer abgleichen etc. auch bei receive
                return fr;
            }
            else{ //TODO: optimieren mit stat_requested oder so, damit nicht jedes mal neu gesendet wird
                vc->need_stat = 1;
                fr = 0;
            }
        }
    }

    return fr;
}

struct nanolink_frame * nanolink_get_blank(struct nanolink_state * inst, uint8_t vc){
    struct nanolink_frame * fr=0;
    if(list_empty(&inst->vc_stats[vc].sendq.list) && !list_empty(&inst->cleanup_list->list)){
        fr = list_first_entry(&inst->cleanup_list->list,struct nanolink_frame,list);
        queue_move(&fr->list,inst->cleanup_list,&inst->vc_stats[vc].sendq);
        memset(&fr->header,0,sizeof(struct nanolink_frame_hdr));
        fr->header.len = HEADER_LEN - 1;
        fr->header.vc = vc;
    }
    return fr;
}

struct nanolink_frame * nanolink_get_next(struct nanolink_state * inst){
    static uint8_t i=0;
    static uint8_t inc = 1;
    int n;
    int empty = 1;
    struct nanolink_frame * fr;
    struct vc_state * vc;

    if(nanolink_need_immediate(inst)){
        return 0;
    }

    for(n=0;n<NUM_CHAN;n++){
        empty = 1;
        uint16_t lo=6; //Sync Marker + CRC len
        vc = &inst->vc_stats[i];
        //first priority: NARQ
        fr = nanolink_select_new(inst,i);
        if(fr != 0 && fr->header.f_arq == 0){
            empty = 0;
        }
        //second: Retransmissions
        else if(inst->vc_stats[i].retransmit){
            fr = nanolink_select_sent(inst,i);
            if(fr != 0){
                empty = 0;
            }
        }
        else if((fr = nanolink_select_new(inst,i)) != 0){
            empty = 0;
        }
        else if((fr = nanolink_select_sent(inst,i)) != 0){
            empty = 0;
        }
        //wenn keine frames mehr da sind, aber send_stat oder so = 1 dummy
        else if(vc->need_stat || vc->send_stat){
            fr = nanolink_get_blank(inst, i);
            empty = 0;
            if(vc->need_stat) lo += 1;
            if(vc->send_stat) lo += nanolink_stat_size(vc); 
        }
         
        if(empty == 1){
            inst->vc_stats[i].deficit = 0;
            i = (i+1)%NUM_CHAN;
            inc = 1;
        }else{
            if(inc){ //may increase
                 vc->deficit += vc->quantum;
                 inc = 0;
            }
            
            if(fr!=0 && fr->header.len+lo <= vc->deficit){
                vc->deficit -= fr->header.len+lo;
                return fr;
            }else{
                i = (i+1)%NUM_CHAN;
                inc = 1;
            }
        }    
    }

    return 0;
}
