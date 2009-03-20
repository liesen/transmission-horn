#include <libgrowl.h>

#include "transmission.h"
#include "growl.h"

struct growl_endpoint_data {
    char * hostname;
    unsigned int port;
    char * password;
};

void growl_completenes_callback(tr_torrent * tor,
                                tr_completeness completeness,
                                void * user_data) {
    free(user_data);
}

void tr_torrentSetGrowlCompletionCallback(tr_torrent * tor,
                                          char * hostname,
                                          unsigned short port,
                                          char * password) {
    struct growl_endpoint_data * g = (struct growl_endpoint_data *)
              malloc(sizeof(struct growl_endpoint_data));

    if (g == NULL) {
        return;
    }

    g->hostname = strdup(hostname);
    g->port = port;
    g->password = strdup(password);

    tr_torrentSetCompletenessCallback(tor,
                                      &growl_completenes_callback,
                                      (void *) g);
}    
