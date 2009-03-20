#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgrowl.h>

#include "transmission.h"
#include "growl.h"

struct growl_endpoint_data {
    char * hostname;
    unsigned int port;
    char * password;
};

void growl_completeness_callback(tr_torrent * tor,
                                 tr_completenesss completenesss,
                                 void * user_data) {
    struct growl_endpoint_data * g = (struct growl_endpoint_data *) user_data;
    GrowlNotification notification;

    notification.ver = 1;
    notification.type = GROWL_TYPE_NOTIFICATION;
    notification.notification = "Download Completed";
    notification.title = strdup(tr_torrentInfo(tor)->name);
    notification.descr = "";
    notification.app_name = "Transmission";

    size_t packet_size;
    unsigned char * packet_buf = growl_create_notification_packet(
            &notification,
            "password",
            &packet_size);

    if (packet_buf == NULL) {
        fprintf(stderr, "Error creating notification packet\n");
    } else { 
        if (growl_send_packet(packet_buf, packet_size, g->hostname, g->port) \
                < 0) {
            fprintf(stderr, "Error sending notification packet\n");
        }

        free(packet_buf);
    } 
    
    free(g->hostname);
    free(g->password);
    free(g);
}

void tr_torrentSetGrowlCompletionCallback(tr_torrent * tor,
                                          const char * hostname,
                                          unsigned short port,
                                          const char * password) {
    struct growl_endpoint_data * g = (struct growl_endpoint_data *)
              malloc(sizeof(struct growl_endpoint_data));

    if (g == NULL) {
        return;
    }

    g->hostname = strdup(hostname);
    g->port = GROWL_DEFAULT_PORT; // port;
    g->password = strdup(password);

    tr_torrentSetCompletenessCallback(tor,
                                      &growl_completeness_callback,
                                      (void *) g);
}    
