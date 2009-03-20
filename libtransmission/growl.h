#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_GROWL_H
#define TR_GROWL_H

#include "transmission.h"

void tr_torrentSetGrowlCompletionCallback(tr_torrent * tor,
                                          char * hostname,
                                          unsigned short port,
                                          char * password);

#endif
