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
    struct growl_endpoint_data * g = (struct growl_endpoint_data *) user_data;
    
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
    g->port = port;
    g->password = strdup(password);

    tr_torrentSetCompletenessCallback(tor,
                                      &growl_completenes_callback,
                                      (void *) g);
}    
