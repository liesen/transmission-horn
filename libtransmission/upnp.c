/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>

#include <miniupnp/miniupnpc.h>
#include <miniupnp/upnpcommands.h>

#include "transmission.h"
#include "port-forwarding.h"
#include "session.h"
#include "utils.h"
#include "upnp.h"

static const char *
getKey( void ) { return _( "Port Forwarding (UPnP)" ); }

typedef enum
{
    TR_UPNP_IDLE,
    TR_UPNP_ERR,
    TR_UPNP_DISCOVER,
    TR_UPNP_MAP,
    TR_UPNP_UNMAP
}
tr_upnp_state;

struct tr_upnp
{
    tr_bool            hasDiscovered;
    struct UPNPUrls    urls;
    struct IGDdatas    data;
    int                port;
    char               lanaddr[16];
    unsigned int       isMapped;
    tr_upnp_state      state;
};

/**
***
**/

tr_upnp*
tr_upnpInit( void )
{
    tr_upnp * ret = tr_new0( tr_upnp, 1 );

    ret->state = TR_UPNP_DISCOVER;
    ret->port = -1;
    return ret;
}

void
tr_upnpClose( tr_upnp * handle )
{
    assert( !handle->isMapped );
    assert( ( handle->state == TR_UPNP_IDLE )
          || ( handle->state == TR_UPNP_ERR )
          || ( handle->state == TR_UPNP_DISCOVER ) );

    if( handle->hasDiscovered )
        FreeUPNPUrls( &handle->urls );
    tr_free( handle );
}

/**
***
**/

int
tr_upnpPulse( tr_upnp * handle,
              int       port,
              int       isEnabled )
{
    int ret;

    if( isEnabled && ( handle->state == TR_UPNP_DISCOVER ) )
    {
        struct UPNPDev * devlist;
        errno = 0;
        devlist = upnpDiscover( 2000, NULL, NULL, 0 );
        if( devlist == NULL )
        {
            tr_ndbg(
                 getKey( ), "upnpDiscover failed (errno %d - %s)", errno,
                tr_strerror( errno ) );
        }
        errno = 0;
        if( UPNP_GetValidIGD( devlist, &handle->urls, &handle->data,
                             handle->lanaddr, sizeof( handle->lanaddr ) ) )
        {
            tr_ninf( getKey( ), _(
                         "Found Internet Gateway Device \"%s\"" ),
                     handle->urls.controlURL );
            tr_ninf( getKey( ), _(
                         "Local Address is \"%s\"" ), handle->lanaddr );
            handle->state = TR_UPNP_IDLE;
            handle->hasDiscovered = 1;
        }
        else
        {
            handle->state = TR_UPNP_ERR;
            tr_ndbg(
                 getKey( ), "UPNP_GetValidIGD failed (errno %d - %s)",
                errno,
                tr_strerror( errno ) );
            tr_ndbg(
                getKey( ),
                "If your router supports UPnP, please make sure UPnP is enabled!" );
        }
        freeUPNPDevlist( devlist );
    }

    if( handle->state == TR_UPNP_IDLE )
    {
        if( handle->isMapped && ( !isEnabled || ( handle->port != port ) ) )
            handle->state = TR_UPNP_UNMAP;
    }

    if( handle->state == TR_UPNP_UNMAP )
    {
        char portStr[16];
        tr_snprintf( portStr, sizeof( portStr ), "%d", handle->port );
        UPNP_DeletePortMapping( handle->urls.controlURL,
                                handle->data.servicetype,
                                portStr, "TCP", NULL );
        tr_ninf( getKey( ),
                 _(
                     "Stopping port forwarding through \"%s\", service \"%s\"" ),
                 handle->urls.controlURL, handle->data.servicetype );
        handle->isMapped = 0;
        handle->state = TR_UPNP_IDLE;
        handle->port = -1;
    }

    if( handle->state == TR_UPNP_IDLE )
    {
        if( isEnabled && !handle->isMapped )
            handle->state = TR_UPNP_MAP;
    }

    if( handle->state == TR_UPNP_MAP )
    {
        int  err = -1;
        errno = 0;

        if( !handle->urls.controlURL || !handle->data.servicetype )
            handle->isMapped = 0;
        else
        {
            char portStr[16];
            char desc[64];
            tr_snprintf( portStr, sizeof( portStr ), "%d", port );
            tr_snprintf( desc, sizeof( desc ), "%s at %d", TR_NAME, port );
            err = UPNP_AddPortMapping( handle->urls.controlURL,
                                       handle->data.servicetype,
                                       portStr, portStr, handle->lanaddr,
                                       desc, "TCP", NULL );
            handle->isMapped = !err;
        }
        tr_ninf( getKey( ),
                 _(
                     "Port forwarding through \"%s\", service \"%s\".  (local address: %s:%d)" ),
                 handle->urls.controlURL, handle->data.servicetype,
                 handle->lanaddr, port );
        if( handle->isMapped )
        {
            tr_ninf( getKey( ), _( "Port forwarding successful!" ) );
            handle->port = port;
            handle->state = TR_UPNP_IDLE;
        }
        else
        {
            tr_ndbg(
                 getKey( ),
                "Port forwarding failed with error %d (errno %d - %s)", err,
                errno, tr_strerror( errno ) );
            tr_ndbg(
                getKey( ),
                "If your router supports UPnP, please make sure UPnP is enabled!" );
            handle->port = -1;
            handle->state = TR_UPNP_ERR;
        }
    }

    switch( handle->state )
    {
        case TR_UPNP_DISCOVER:
            ret = TR_PORT_UNMAPPED; break;

        case TR_UPNP_MAP:
            ret = TR_PORT_MAPPING; break;

        case TR_UPNP_UNMAP:
            ret = TR_PORT_UNMAPPING; break;

        case TR_UPNP_IDLE:
            ret = handle->isMapped ? TR_PORT_MAPPED
                  : TR_PORT_UNMAPPED; break;

        default:
            ret = TR_PORT_ERROR; break;
    }

    return ret;
}

