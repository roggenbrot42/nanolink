#ifndef NANOLINK_SCHEDULER_H
#define NANOLINK_SCHEDULER_H

#include "types.h"
#include "list.h"
#include "nanolink.h"

/* Nanolink Deficit Round Robin Implementation */

struct nanolink_frame * nanolink_get_next(struct nanolink_state *);
struct nanolink_frame * nanolink_select_new(struct nanolink_state * ,uint8_t);
struct nanolink_frame * nanolink_select_sent(struct nanolink_state * ,uint8_t);



#endif
