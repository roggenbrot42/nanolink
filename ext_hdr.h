#ifndef __EXT_HDR_H
#define __EXT_HDR_H

#include "types.h"

struct stat_hdr {

    octet fields;
    
    octet len;
    octet lr;
    octet rr;   
    octet * entries;
};

#endif
