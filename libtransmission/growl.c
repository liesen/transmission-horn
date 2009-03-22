#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgrowl.h>

#include "transmission.h"
#include "torrent.h"
#include "session.h" // TR_NAME
#include "growl.h"
#include "utils.h"

#define GROWL_DOWNLOAD_COMPLETE "Download Complete"

// Holds data needed to talk to Growl at the receiving end
struct growl_endpoint_data {
    char * hostname;
    unsigned short port;
    char * password;
};

void growl_completeness_callback(tr_torrent * torrent,
                                 tr_completeness completeness,
                                 void * user_data) {
    struct growl_endpoint_data * g;
    GrowlNotification notification;
    size_t packet_size;
    const char * packet_buf;

    tr_dbg("Growl completess callback");

    if (completeness != TR_SEED) {
        tr_dbg("Torrent not complete yet");
        return;
    }

    g = (struct growl_endpoint_data *) user_data;
    notification.ver = GROWL_PROTOCOL_VERSION;
    notification.type = GROWL_TYPE_NOTIFICATION;
    notification.notification = GROWL_DOWNLOAD_COMPLETE; 
    notification.title = GROWL_DOWNLOAD_COMPLETE; 
    notification.descr = tr_torrentInfo(torrent)->name;
    notification.app_name = TR_NAME;

    packet_buf = growl_create_notification_packet(
            &notification,
            g->password == NULL ? "" : g->password,
            &packet_size);

    if (packet_buf == NULL) {
        tr_dbg("Error creating Growl notification packet");
    } else { 
        if (growl_send_packet(packet_buf, packet_size, g->hostname, g->port) \
                < 0) {
            tr_dbg("Error sending Growl notification packet");
        }

        free(packet_buf);
    } 
    
    free(g->hostname);
    free(g->password);
    free(g);
}

void tr_torrentSetGrowlCompletionCallback(tr_torrent * torrent,
                                          const char * hostname,
                                          unsigned short port,
                                          const char * password) {
    struct growl_endpoint_data * g; 

    // Don't set callback on complete torrent
    if (torrent->completeness == TR_SEED) {
        return;
    }

    if (hostname == NULL) {
        return;
    }

    g = (struct growl_endpoint_data *)
              malloc(sizeof(struct growl_endpoint_data));

    if (g == NULL) {
        tr_dbg("Failed to allocate Growl end point data; aborting");
        return;
    }

    g->hostname = strdup(hostname);
    g->port = port == 0 ? GROWL_DEFAULT_PORT : port;
    g->password = password == NULL ? NULL : strdup(password);
    tr_torrentSetCompletenessCallback(torrent,
                                      &growl_completeness_callback,
                                      (void *) g);
}
