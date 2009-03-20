#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include "transmission.h"

void tr_torrentSetGrowlCompletionCallback(tr_torrent * tor,
                                          const char * hostname,
                                          unsigned short port,
                                          const char * password);

#endif
