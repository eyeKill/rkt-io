#ifndef _PING_H
#define _PING_H

#include <stdint.h>

// useful for debugging
int send_ping(int portid, struct rte_mempool* txpool);

#endif
