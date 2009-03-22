# Transmission: Horn

Adding Growl notifications to the Transmission *daemon*. Notifications can be
requested with the RPC protocol (<http://trac.transmissionbt.com/browser/trunk/doc/rpc-spec.txt>)

### Example

    { "method": "torrent-set",
      "arguments": {
        "ids": [1, 2, ...],
        "growl-host": "192.168.1.2",
        "growl-port": 8997, # optional (uses default Growl port if omitted)
        "growl-password": "banana" # optional
      }
    } 

