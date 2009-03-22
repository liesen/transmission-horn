#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_GROWL_H
#define TR_GROWL_H

#include "transmission.h"

void tr_torrentSetGrowlCompletionCallback(tr_torrent * torrent,
                                          const char * hostname,
                                          unsigned short port,
                                          const char * password);

#endif
