#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgrowl.h>

#include "transmission.h"
#include "growl.h"
#include "utils.h"

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
    tr_dbg("Growl completess callback\n");
    struct growl_endpoint_data * g = (struct growl_endpoint_data *) user_data;
    GrowlNotification notification;

    notification.ver = 1;
    notification.type = GROWL_TYPE_NOTIFICATION;
    notification.notification = "Download Completed";
    notification.title = strdup(tr_torrentInfo(torrent)->name);
    notification.descr = "";
    notification.app_name = "Transmission";

    tr_dbg("Creating notification packet\n");
    size_t packet_size;
    unsigned char * packet_buf = growl_create_notification_packet(
            &notification,
            GROWL_PASSWORD,
            &packet_size);

    if (packet_buf == NULL) {
        tr_dbg("Error creating notification packet\n");
    } else { 
        if (growl_send_packet(packet_buf, packet_size, g->hostname, g->port) \
                < 0) {
            tr_dbg("Error sending notification packet\n");
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
    tr_dbg("Attaching Growl completion callback\n");

    tr_dbg("Allocating end point data\n");
    struct growl_endpoint_data * g = (struct growl_endpoint_data *)
              malloc(sizeof(struct growl_endpoint_data));

    if (g == NULL) {
        tr_dbg("Failed to allocate--aborting\n");
        return;
    }

    g->hostname = strdup(hostname);
    g->port = port >= 0 ? port : GROWL_DEFAULT_PORT;
    g->password = strdup(password);

    tr_dbg("Storing Growl callback to %s:%d\n", g->hostname, g->port);
    tr_torrentSetCompletenessCallback(torrent,
                                      &growl_completeness_callback,
                                      (void *) g);
}    
