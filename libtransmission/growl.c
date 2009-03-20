#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgrowl.h>

#include "transmission.h"
#include "session.h" // TR_NAME
#include "growl.h"
#include "utils.h"

#define TR_GROWL_COMPLETED_NOTIFICATION "Download Complete"
#define GROWL_PASSWORD "password"

// Holds data needed to talk to Growl at the receiving end
struct growl_endpoint_data {
    char * hostname;
    unsigned int port;
    char * password;
};

void growl_completeness_callback(tr_torrent * torrent,
                                 tr_completeness completeness,
                                 void * user_data) {
    tr_inf("Growl completess callback");

    if (completeness != TR_SEED) {
        tr_inf("Torrent not complete yet");
        // return;
    }

    struct growl_endpoint_data * g = (struct growl_endpoint_data *) user_data;
    GrowlNotification notification;

    notification.ver = 1;
    notification.type = GROWL_TYPE_NOTIFICATION;
    notification.notification = TR_GROWL_COMPLETED_NOTIFICATION; 
    notification.title = strdup(tr_torrentInfo(torrent)->name);
    notification.descr = "";
    notification.app_name = TR_NAME;

    tr_inf("Creating notification packet");
    size_t packet_size;
    unsigned char * packet_buf = growl_create_notification_packet(
            &notification,
            g->password == NULL ? GROWL_PASSWORD : g->password,
            &packet_size);

    if (packet_buf == NULL) {
        tr_inf("Error creating notification packet");
    } else { 
        if (growl_send_packet(packet_buf, packet_size, g->hostname, g->port) \
                < 0) {
            tr_inf("Error sending notification packet");
        }

        tr_inf("Sent notification packet to %s:%d: %s", g->hostname, g->port,
                notification.title);
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
    tr_inf("Attaching Growl completion callback");
    struct growl_endpoint_data * g = (struct growl_endpoint_data *)
              malloc(sizeof(struct growl_endpoint_data));

    if (g == NULL) {
        tr_inf("Failed to allocate end point data--aborting");
        return;
    }

    g->hostname = strdup(hostname);
    g->port = port > 0 ? port : GROWL_DEFAULT_PORT;
    g->password = password == NULL ? NULL : strdup(password);
    /* tr_torrentSetCompletenessCallback(torrent,
                                      &growl_completeness_callback,
                                      (void *) g);
    */
    growl_completeness_callback(torrent, TR_SEED, (void *) g);
}    
