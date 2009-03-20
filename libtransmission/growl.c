#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgrowl.h>

#include "transmission.h"
#include "torrent.h"
#include "session.h" // TR_NAME
#include "growl.h"
#include "utils.h"

#define TR_GROWL_COMPLETED_NOTIFICATION "Download Complete"
#define GROWL_PASSWORD "password"

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

    tr_inf("Growl completess callback");

    if (completeness != TR_SEED) {
        tr_inf("Torrent not complete yet");
        return;
    }

    g = (struct growl_endpoint_data *) user_data;
    notification.ver = GROWL_PROTOCOL_VERSION;
    notification.type = GROWL_TYPE_NOTIFICATION;
    notification.notification = TR_GROWL_COMPLETED_NOTIFICATION; 
    notification.title = TR_GROWL_COMPLETED_NOTIFICATION; 
    notification.descr = tr_torrentInfo(torrent)->name;
    notification.app_name = TR_NAME;

    packet_buf = growl_create_notification_packet(
            &notification,
            g->password == NULL ? GROWL_PASSWORD : g->password,
            &packet_size);

    if (packet_buf == NULL) {
        tr_dbg("Error creating Growl notification packet");
    } else { 
        if (growl_send_packet(packet_buf, packet_size, g->hostname, g->port) \
                < 0) {
            tr_inf("Error sending Growl notification packet");
        }

        free(packet_buf);
    } 
    
    free(g->hostname);
    free(g->password);
    free(g);
}

void tr_torrentSetGrowlCompletionCallback(tr_torrent * torrent,
                                          char * hostname,
                                          short port,
                                          char * password) {
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
    g->port = port > 0 ? (unsigned short) port : GROWL_DEFAULT_PORT;
    g->password = password == NULL ? NULL : strdup(password);
    tr_torrentSetCompletenessCallback(torrent,
                                      &growl_completeness_callback,
                                      (void *) g);
}
