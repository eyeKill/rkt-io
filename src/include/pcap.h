#ifndef _PCAP_H
#define _PCAP_H

#include "stddef.h"

#ifdef DEBUG
int write_pcap_file(const char* filename, void* pkt, size_t len);
#endif

#endif
