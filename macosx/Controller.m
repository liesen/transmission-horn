/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2005-2009 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import <IOKit/IOMessage.h>
#import <IOKit/pwr_mgt/IOPMLib.h>

#import "Controller.h"
#import "Torrent.h"
#import "TorrentGroup.h"
#import "TorrentCell.h"
#import "TorrentTableView.h"
#import "CreatorWindowController.h"
#import "StatsWindowController.h"
#import "QuickLookController.h"
#import "GroupsController.h"
#import "AboutWindowController.h"
#import "ButtonToolbarItem.h"
#import "GroupToolbarItem.h"
#import "ToolbarSegmentedCell.h"
#import "BlocklistDownloader.h"
#import "StatusBarView.h"
#import "FilterButton.h"
#import "BonjourController.h"
#import "NSStringAdditions.h"
#import "ExpandedPathToPathTransformer.h"
#import "ExpandedPathToIconTransformer.h"
#import "bencode.h"
#import "utils.h"

#import "UKKQueue.h"
#import <Sparkle/Sparkle.h>

#define TOOLBAR_CREATE                  @"Toolbar Create"
#define TOOLBAR_OPEN_FILE               @"Toolbar Open"
#define TOOLBAR_OPEN_WEB                @"Toolbar Open Web"
#define TOOLBAR_REMOVE                  @"Toolbar Remove"
#define TOOLBAR_INFO                    @"Toolbar Info"
#define TOOLBAR_PAUSE_ALL               @"Toolbar Pause All"
#define TOOLBAR_RESUME_ALL              @"Toolbar Resume All"
#define TOOLBAR_PAUSE_RESUME_ALL        @"Toolbar Pause / Resume All"
#define TOOLBAR_PAUSE_SELECTED          @"Toolbar Pause Selected"
#define TOOLBAR_RESUME_SELECTED         @"Toolbar Resume Selected"
#define TOOLBAR_PAUSE_RESUME_SELECTED   @"Toolbar Pause / Resume Selected"
#define TOOLBAR_FILTER                  @"Toolbar Toggle Filter"
#define TOOLBAR_QUICKLOOK               @"Toolbar QuickLook"

typedef enum
{
    TOOLBAR_PAUSE_TAG = 0,
    TOOLBAR_RESUME_TAG = 1
} toolbarGroupTag;

#define SORT_DATE       @"Date"
#define SORT_NAME       @"Name"
#define SORT_STATE      @"State"
#define SORT_PROGRESS   @"Progress"
#define SORT_TRACKER    @"Tracker"
#define SORT_ORDER      @"Order"
#define SORT_ACTIVITY   @"Activity"

typedef enum
{
    SORT_ORDER_TAG = 0,
    SORT_DATE_TAG = 1,
    SORT_NAME_TAG = 2,
    SORT_PROGRESS_TAG = 3,
    SORT_STATE_TAG = 4,
    SORT_TRACKER_TAG = 5,
    SORT_ACTIVITY_TAG = 6
} sortTag;

#define FILTER_NONE     @"None"
#define FILTER_ACTIVE   @"Active"
#define FILTER_DOWNLOAD @"Download"
#define FILTER_SEED     @"Seed"
#define FILTER_PAUSE    @"Pause"

#define FILTER_TYPE_NAME    @"Name"
#define FILTER_TYPE_TRACKER @"Tracker"

#define FILTER_TYPE_TAG_NAME    401
#define FILTER_TYPE_TAG_TRACKER 402

#define GROUP_FILTER_ALL_TAG    -2

#define STATUS_RATIO_TOTAL      @"RatioTotal"
#define STATUS_RATIO_SESSION    @"RatioSession"
#define STATUS_TRANSFER_TOTAL   @"TransferTotal"
#define STATUS_TRANSFER_SESSION @"TransferSession"

typedef enum
{
    STATUS_RATIO_TOTAL_TAG = 0,
    STATUS_RATIO_SESSION_TAG = 1,
    STATUS_TRANSFER_TOTAL_TAG = 2,
    STATUS_TRANSFER_SESSION_TAG = 3
} statusTag;

#define GROWL_DOWNLOAD_COMPLETE @"Download Complete"
#define GROWL_SEEDING_COMPLETE  @"Seeding Complete"
#define GROWL_AUTO_ADD          @"Torrent Auto Added"
#define GROWL_AUTO_SPEED_LIMIT  @"Speed Limit Auto Changed"

#define TORRENT_TABLE_VIEW_DATA_TYPE    @"TorrentTableViewDataType"

#define ROW_HEIGHT_REGULAR      62.0
#define ROW_HEIGHT_SMALL        38.0
#define WINDOW_REGULAR_WIDTH    468.0

#define SEARCH_FILTER_MIN_WIDTH 48.0
#define SEARCH_FILTER_MAX_WIDTH 95.0

#define UPDATE_UI_SECONDS   1.0

#define DOCK_SEEDING_TAG        101
#define DOCK_DOWNLOADING_TAG    102

#define SUPPORT_FOLDER  @"/Library/Application Support/Transmission/Transfers.plist"

#define WEBSITE_URL @"http://www.transmissionbt.com/"
#define FORUM_URL   @"http://forum.transmissionbt.com/"
#define TRAC_URL   @"http://trac.transmissionbt.com/"
#define DONATE_URL  @"http://www.transmissionbt.com/donate.php"

static tr_rpc_callback_status rpcCallback(tr_session * handle UNUSED, tr_rpc_callback_type type, struct tr_torrent * torrentStruct, void * controller)
{
    [(Controller *)controller rpcCallback: type forTorrentStruct: torrentStruct];
    return TR_RPC_NOREMOVE; //we'll do the remove manually
}

static void sleepCallback(void * controller, io_service_t y, natural_t messageType, void * messageArgument)
{
    [(Controller *)controller sleepCallback: messageType argument: messageArgument];
}

@implementation Controller

+ (void) initialize
{
    //make sure another Transmission.app isn't running already
    NSString * bundleIdentifier = [[NSBundle mainBundle] bundleIdentifier];
    int processIdentifier = [[NSProcessInfo processInfo] processIdentifier];

    for (NSDictionary * dic in [[NSWorkspace sharedWorkspace] launchedApplications])
    {
        if ([[dic objectForKey: @"NSApplicationBundleIdentifier"] isEqualToString: bundleIdentifier]
            && [[dic objectForKey: @"NSApplicationProcessIdentifier"] intValue] != processIdentifier)
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle: NSLocalizedString(@"Quit", "Transmission already running alert -> button")];
            [alert setMessageText: NSLocalizedString(@"Transmission is already running.",
                                                    "Transmission already running alert -> title")];
            [alert setInformativeText: NSLocalizedString(@"There is already a copy of Transmission running. "
                "This copy cannot be opened until that instance is quit.", "Transmission already running alert -> message")];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert runModal];
            [alert release];
            
            //kill ourselves right away
            exit(0);
        }
    }
    
    [[NSUserDefaults standardUserDefaults] registerDefaults: [NSDictionary dictionaryWithContentsOfFile:
        [[NSBundle mainBundle] pathForResource: @"Defaults" ofType: @"plist"]]];
    
    //set custom value transformers
    ExpandedPathToPathTransformer * pathTransformer = [[[ExpandedPathToPathTransformer alloc] init] autorelease];
    [NSValueTransformer setValueTransformer: pathTransformer forName: @"ExpandedPathToPathTransformer"];
    
    ExpandedPathToIconTransformer * iconTransformer = [[[ExpandedPathToIconTransformer alloc] init] autorelease];
    [NSValueTransformer setValueTransformer: iconTransformer forName: @"ExpandedPathToIconTransformer"];
}

- (id) init
{
    if ((self = [super init]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        tr_benc settings;
        tr_bencInitDict(&settings, 22);
        tr_sessionGetDefaultSettings(&settings);
        
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, [fDefaults boolForKey: @"Blocklist"]);
        
        #warning update when changing in prefs
        tr_bencDictAddStr(&settings, TR_PREFS_KEY_DOWNLOAD_DIR, [[[fDefaults stringForKey: @"DownloadFolder"]
                                                                    stringByExpandingTildeInPath] UTF8String]);
        
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_MSGLEVEL, [fDefaults integerForKey: @"MessageLevel"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, [fDefaults integerForKey: @"PeersTotal"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_PEER_LIMIT_TORRENT, [fDefaults integerForKey: @"PeersTorrent"]);
        
        const BOOL randomPort = [fDefaults boolForKey: @"RandomPort"];
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_PEER_PORT_RANDOM_ENABLED, randomPort);
        if (!randomPort)
            tr_bencDictAddInt(&settings, TR_PREFS_KEY_PEER_PORT, [fDefaults integerForKey: @"BindPort"]);
        
        //hidden pref
        if ([fDefaults objectForKey: @"PeerSocketTOS"])
            tr_bencDictAddInt(&settings, TR_PREFS_KEY_PEER_SOCKET_TOS, [fDefaults integerForKey: @"PeerSocketTOS"]);
        
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_PEX_ENABLED, [fDefaults boolForKey: @"PEXGlobal"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_PORT_FORWARDING, [fDefaults boolForKey: @"NatTraversal"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_PROXY_AUTH_ENABLED, [fDefaults boolForKey: @"ProxyAuthorize"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_PROXY_ENABLED, [fDefaults boolForKey: @"Proxy"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_PROXY_PORT, [fDefaults integerForKey: @"ProxyPort"]);
        tr_bencDictAddStr(&settings, TR_PREFS_KEY_PROXY, [[fDefaults stringForKey: @"ProxyAddress"] UTF8String]);
        tr_bencDictAddStr(&settings, TR_PREFS_KEY_PROXY_USERNAME,  [[fDefaults stringForKey: @"ProxyUsername"] UTF8String]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED,  [fDefaults boolForKey: @"RPCAuthorize"]);
        tr_bencDictAddDouble(&settings, TR_PREFS_KEY_RATIO, [fDefaults floatForKey: @"RatioLimit"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_RATIO_ENABLED, [fDefaults boolForKey: @"RatioCheck"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_RPC_ENABLED,  [fDefaults boolForKey: @"RPC"]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_RPC_PORT,  [fDefaults integerForKey: @"RPCPort"]);
        tr_bencDictAddStr(&settings, TR_PREFS_KEY_RPC_USERNAME,  [[fDefaults stringForKey: @"RPCUsername"] UTF8String]);
        tr_bencDictAddInt(&settings, TR_PREFS_KEY_RPC_WHITELIST_ENABLED,  [fDefaults boolForKey: @"RPCUseWhitelist"]);
        
        fLib = tr_sessionInit("macosx", tr_getDefaultConfigDir("Transmission"), YES, &settings);
        tr_bencFree(&settings);
        
        [NSApp setDelegate: self];
        
        fTorrents = [[NSMutableArray alloc] init];
        fDisplayedTorrents = [[NSMutableArray alloc] init];
        
        fMessageController = [[MessageWindowController alloc] init];
        fInfoController = [[InfoWindowController alloc] init];
        
        [PrefsController setHandle: fLib];
        fPrefsController = [[PrefsController alloc] init];
        
        [QuickLookController quickLookControllerInitializeWithController: self infoController: fInfoController];
        
        fSoundPlaying = NO;
        
        tr_sessionSetRPCCallback(fLib, rpcCallback, self);
        
        [GrowlApplicationBridge setGrowlDelegate: self];
        
        [[UKKQueue sharedFileWatcher] setDelegate: self];
        
        [[SUUpdater sharedUpdater] setDelegate: self];
        fUpdateInProgress = NO;
    }
    return self;
}

- (void) awakeFromNib
{
    NSToolbar * toolbar = [[NSToolbar alloc] initWithIdentifier: @"TRMainToolbar"];
    [toolbar setDelegate: self];
    [toolbar setAllowsUserCustomization: YES];
    [toolbar setAutosavesConfiguration: YES];
    [toolbar setDisplayMode: NSToolbarDisplayModeIconOnly];
    [fWindow setToolbar: toolbar];
    [toolbar release];
    
    [fWindow setDelegate: self]; //do manually to avoid placement issue
    
    [fWindow makeFirstResponder: fTableView];
    [fWindow setExcludedFromWindowsMenu: YES];
    
    //set table size
    if ([fDefaults boolForKey: @"SmallView"])
        [fTableView setRowHeight: ROW_HEIGHT_SMALL];
    
    //window min height
    NSSize contentMinSize = [fWindow contentMinSize];
    contentMinSize.height = [[fWindow contentView] frame].size.height - [[fTableView enclosingScrollView] frame].size.height
                                + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    [fWindow setContentMinSize: contentMinSize];
    [fWindow setContentBorderThickness: [[fTableView enclosingScrollView] frame].origin.y forEdge: NSMinYEdge];
        
    [[fTotalTorrentsField cell] setBackgroundStyle: NSBackgroundStyleRaised];
    
    [self updateGroupsFilterButton];
    
    //set up filter bar
    NSView * contentView = [fWindow contentView];
    NSSize windowSize = [contentView convertSize: [fWindow frame].size fromView: nil];
    [fFilterBar setHidden: YES];
    
    NSRect filterBarFrame = [fFilterBar frame];
    filterBarFrame.size.width = windowSize.width;
    [fFilterBar setFrame: filterBarFrame];
    
    [contentView addSubview: fFilterBar];
    [fFilterBar setFrameOrigin: NSMakePoint(0, NSMaxY([contentView frame]))];

    [self showFilterBar: [fDefaults boolForKey: @"FilterBar"] animate: NO];
    
    //set up status bar
    [fStatusBar setHidden: YES];
    
    [self updateSpeedFieldsToolTips];
    
    NSRect statusBarFrame = [fStatusBar frame];
    statusBarFrame.size.width = windowSize.width;
    [fStatusBar setFrame: statusBarFrame];
    
    [contentView addSubview: fStatusBar];
    [fStatusBar setFrameOrigin: NSMakePoint(0, NSMaxY([contentView frame]))];
    [self showStatusBar: [fDefaults boolForKey: @"StatusBar"] animate: NO];
    
    [fActionButton setToolTip: NSLocalizedString(@"Shortcuts for changing global settings.",
                                "Main window -> 1st bottom left button (action) tooltip")];
    [fSpeedLimitButton setToolTip: NSLocalizedString(@"Speed Limit overrides the total bandwidth limits with its own limits.",
                                "Main window -> 2nd bottom left button (turtle) tooltip")];
    
    [fTableView registerForDraggedTypes: [NSArray arrayWithObject: TORRENT_TABLE_VIEW_DATA_TYPE]];
    [fWindow registerForDraggedTypes: [NSArray arrayWithObjects: NSFilenamesPboardType, NSURLPboardType, nil]];

    //register for sleep notifications
    IONotificationPortRef notify;
    io_object_t iterator;
    if ((fRootPort = IORegisterForSystemPower(self, & notify, sleepCallback, &iterator)))
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notify), kCFRunLoopCommonModes);
    else
        NSLog(@"Could not IORegisterForSystemPower");
    
    //load previous transfers
    NSArray * history = [[NSArray alloc] initWithContentsOfFile: [NSHomeDirectory() stringByAppendingPathComponent: SUPPORT_FOLDER]];
    
    //old version saved transfer info in prefs file
    if (!history)
    {
        if ((history = [fDefaults arrayForKey: @"History"]))
        {
            [history retain];
            [fDefaults removeObjectForKey: @"History"];
        }
    }
    
    if (history)
    {
        for (NSDictionary * historyItem in history)
        {
            Torrent * torrent;
            if ((torrent = [[Torrent alloc] initWithHistory: historyItem lib: fLib]))
            {
                [fTorrents addObject: torrent];
                [torrent release];
            }
        }
        [history release];
    }
    
    //set filter
    NSString * filterType = [fDefaults stringForKey: @"Filter"];
    
    NSButton * currentFilterButton;
    if ([filterType isEqualToString: FILTER_ACTIVE])
        currentFilterButton = fActiveFilterButton;
    else if ([filterType isEqualToString: FILTER_PAUSE])
        currentFilterButton = fPauseFilterButton;
    else if ([filterType isEqualToString: FILTER_SEED])
        currentFilterButton = fSeedFilterButton;
    else if ([filterType isEqualToString: FILTER_DOWNLOAD])
        currentFilterButton = fDownloadFilterButton;
    else
    {
        //safety
        if (![filterType isEqualToString: FILTER_NONE])
            [fDefaults setObject: FILTER_NONE forKey: @"Filter"];
        currentFilterButton = fNoFilterButton;
    }
    [currentFilterButton setState: NSOnState];
    
    //set filter search type
    NSString * filterSearchType = [fDefaults stringForKey: @"FilterSearchType"];
    
    NSMenu * filterSearchMenu = [[fSearchFilterField cell] searchMenuTemplate];
    NSString * filterSearchTypeTitle;
    if ([filterSearchType isEqualToString: FILTER_TYPE_TRACKER])
        filterSearchTypeTitle = [[filterSearchMenu itemWithTag: FILTER_TYPE_TAG_TRACKER] title];
    else
    {
        //safety
        if (![filterType isEqualToString: FILTER_TYPE_NAME])
            [fDefaults setObject: FILTER_TYPE_NAME forKey: @"FilterSearchType"];
        filterSearchTypeTitle = [[filterSearchMenu itemWithTag: FILTER_TYPE_TAG_NAME] title];
    }
    [[fSearchFilterField cell] setPlaceholderString: filterSearchTypeTitle];
    
    fBadger = [[Badger alloc] initWithLib: fLib];
    
    //observe notifications
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    
    [nc addObserver: self selector: @selector(updateUI)
                    name: @"UpdateUI" object: nil];
    
    [nc addObserver: self selector: @selector(torrentFinishedDownloading:)
                    name: @"TorrentFinishedDownloading" object: nil];
    
    [nc addObserver: self selector: @selector(torrentRestartedDownloading:)
                    name: @"TorrentRestartedDownloading" object: nil];
    
    //avoids need of setting delegate
    [nc addObserver: self selector: @selector(torrentTableViewSelectionDidChange:)
                    name: NSOutlineViewSelectionDidChangeNotification object: fTableView];
    
    [nc addObserver: self selector: @selector(autoSpeedLimitChange:)
                    name: @"AutoSpeedLimitChange" object: nil];
    
    [nc addObserver: self selector: @selector(changeAutoImport)
                    name: @"AutoImportSettingChange" object: nil];
    
    [nc addObserver: self selector: @selector(setWindowSizeToFit)
                    name: @"AutoSizeSettingChange" object: nil];
    
    [nc addObserver: self selector: @selector(updateForExpandCollape)
                    name: @"OutlineExpandCollapse" object: nil];
    
    [nc addObserver: fWindow selector: @selector(makeKeyWindow)
                    name: @"MakeWindowKey" object: nil];
    
    //check if torrent should now start
    [nc addObserver: self selector: @selector(torrentStoppedForRatio:)
                    name: @"TorrentStoppedForRatio" object: nil];
    
    [nc addObserver: self selector: @selector(updateTorrentsInQueue)
                    name: @"UpdateQueue" object: nil];
    
    //open newly created torrent file
    [nc addObserver: self selector: @selector(beginCreateFile:)
                    name: @"BeginCreateTorrentFile" object: nil];
    
    //open newly created torrent file
    [nc addObserver: self selector: @selector(openCreatedFile:)
                    name: @"OpenCreatedTorrentFile" object: nil];
    
    //update when groups change
    [nc addObserver: self selector: @selector(updateGroupsFilters:)
                    name: @"UpdateGroups" object: nil];
    
    //update when speed limits are changed
    [nc addObserver: self selector: @selector(updateSpeedFieldsToolTips)
                    name: @"SpeedLimitUpdate" object: nil];

    //timer to update the interface every second
    [self updateUI];
    fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_UI_SECONDS target: self
                selector: @selector(updateUI) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSEventTrackingRunLoopMode];
    
    [self applyFilter: nil];
    
    [fWindow makeKeyAndOrderFront: nil]; 
    
    if ([fDefaults boolForKey: @"InfoVisible"])
        [self showInfo: nil];
    
    //set up the speed limit
    [self autoSpeedLimitChange: nil];
}

- (void) applicationDidFinishLaunching: (NSNotification *) notification
{
    [NSApp setServicesProvider: self];
    
    //register for dock icon drags
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler: self andSelector: @selector(handleOpenContentsEvent:replyEvent:)
        forEventClass: kCoreEventClass andEventID: kAEOpenContents];
    
    //auto importing
    [self checkAutoImportDirectory];
    
    //registering the Web UI to Bonjour
    if ([fDefaults boolForKey: @"RPC"] && [fDefaults boolForKey: @"RPCWebDiscovery"])
        [[BonjourController defaultController] startWithPort: [fDefaults integerForKey: @"RPCPort"]];
}

- (BOOL) applicationShouldHandleReopen: (NSApplication *) app hasVisibleWindows: (BOOL) visibleWindows
{
    if (![fWindow isVisible] && ![[fPrefsController window] isVisible])
        [fWindow makeKeyAndOrderFront: nil];
    return NO;
}

- (NSApplicationTerminateReply) applicationShouldTerminate: (NSApplication *) sender
{
    if (!fUpdateInProgress && [fDefaults boolForKey: @"CheckQuit"])
    {
        NSInteger active = 0, downloading = 0;
        for (Torrent * torrent  in fTorrents)
            if ([torrent isActive] && ![torrent isStalled])
            {
                active++;
                if (![torrent allDownloaded])
                    downloading++;
            }
        
        if ([fDefaults boolForKey: @"CheckQuitDownloading"] ? downloading > 0 : active > 0)
        {
            NSString * message = active == 1
                ? NSLocalizedString(@"There is an active transfer that will be paused on quit."
                    " The transfer will automatically resume on the next launch.", "Confirm Quit panel -> message")
                : [NSString stringWithFormat: NSLocalizedString(@"There are %d active transfers that will be paused on quit."
                    " The transfers will automatically resume on the next launch.", "Confirm Quit panel -> message"), active];

            NSBeginAlertSheet(NSLocalizedString(@"Are you sure you want to quit?", "Confirm Quit panel -> title"),
                                NSLocalizedString(@"Quit", "Confirm Quit panel -> button"),
                                NSLocalizedString(@"Cancel", "Confirm Quit panel -> button"), nil, fWindow, self,
                                @selector(quitSheetDidEnd:returnCode:contextInfo:), nil, nil, message);
            return NSTerminateLater;
        }
    }
    
    return NSTerminateNow;
}

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo
{
    [NSApp replyToApplicationShouldTerminate: returnCode == NSAlertDefaultReturn];
}

- (void) applicationWillTerminate: (NSNotification *) notification
{
    //stop the Bonjour service
    [[BonjourController defaultController] stop];

    //stop blocklist download
    if ([BlocklistDownloader isRunning])
        [[BlocklistDownloader downloader] cancelDownload];
    
    //stop timers and notification checking
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fTimer invalidate];
    [fSpeedLimitTimer invalidate];
    
    if (fAutoImportTimer)
    {   
        if ([fAutoImportTimer isValid])
            [fAutoImportTimer invalidate];
        [fAutoImportTimer release];
    }
    
    [fBadger setQuitting];
    
    //remove all torrent downloads
    if (fPendingTorrentDownloads)
    {
        for (NSDictionary * downloadDict in fPendingTorrentDownloads)
        {
            NSURLDownload * download = [downloadDict objectForKey: @"Download"];
            [download cancel];
            [download release];
        }
        [fPendingTorrentDownloads removeAllObjects];
    }
    
    //remember window states and close all windows
    [fDefaults setBool: [[fInfoController window] isVisible] forKey: @"InfoVisible"];
    [[NSApp windows] makeObjectsPerformSelector: @selector(close)];
    [self showStatusBar: NO animate: NO];
    [self showFilterBar: NO animate: NO];
    
    //save history
    [self updateTorrentHistory];
    [fTableView saveCollapsedGroups];
    
    //remaining calls the same as dealloc 
    [fInfoController release];
    [fMessageController release];
    [fPrefsController release];
    
    [fTorrents release];
    [fDisplayedTorrents release];
    
    [fOverlayWindow release];
    [fBadger release];
    
    [fAutoImportedNames release];
    [fPendingTorrentDownloads release];
    
    //complete cleanup
    tr_sessionClose(fLib);
}

- (void) handleOpenContentsEvent: (NSAppleEventDescriptor *) event replyEvent: (NSAppleEventDescriptor *) replyEvent
{
    NSString * urlString = nil;

    NSAppleEventDescriptor * directObject = [event paramDescriptorForKeyword: keyDirectObject];
    if ([directObject descriptorType] == typeAEList)
    {
        for (NSUInteger i = 1; i <= [directObject numberOfItems]; i++)
            if ((urlString = [[directObject descriptorAtIndex: i] stringValue]))
                break;
    }
    else
        urlString = [directObject stringValue];
    
    if (urlString)
        [self openURL: [NSURL URLWithString: urlString]];
}

- (void) download: (NSURLDownload *) download decideDestinationWithSuggestedFilename: (NSString *) suggestedName
{
    if ([[suggestedName pathExtension] caseInsensitiveCompare: @"torrent"] != NSOrderedSame)
    {
        [download cancel];
        
        NSRunAlertPanel(NSLocalizedString(@"Torrent download failed", "Download not a torrent -> title"),
            [NSString stringWithFormat: NSLocalizedString(@"It appears that the file \"%@\" from %@ is not a torrent file.",
            "Download not a torrent -> message"), suggestedName,
            [[[[download request] URL] absoluteString] stringByReplacingPercentEscapesUsingEncoding: NSUTF8StringEncoding]],
            NSLocalizedString(@"OK", "Download not a torrent -> button"), nil, nil);
        
        [download release];
    }
    else
        [download setDestination: [NSTemporaryDirectory() stringByAppendingPathComponent: [suggestedName lastPathComponent]]
                    allowOverwrite: NO];
}

-(void) download: (NSURLDownload *) download didCreateDestination: (NSString *) path
{
    if (!fPendingTorrentDownloads)
        fPendingTorrentDownloads = [[NSMutableDictionary alloc] init];
    
    [fPendingTorrentDownloads setObject: [NSDictionary dictionaryWithObjectsAndKeys:
                    path, @"Path", download, @"Download", nil] forKey: [[download request] URL]];
}

- (void) download: (NSURLDownload *) download didFailWithError: (NSError *) error
{
    NSRunAlertPanel(NSLocalizedString(@"Torrent download failed", "Torrent download error -> title"),
        [NSString stringWithFormat: NSLocalizedString(@"The torrent could not be downloaded from %@: %@.",
        "Torrent download failed -> message"),
        [[[[download request] URL] absoluteString] stringByReplacingPercentEscapesUsingEncoding: NSUTF8StringEncoding],
        [error localizedDescription]], NSLocalizedString(@"OK", "Torrent download failed -> button"), nil, nil);
    
    [fPendingTorrentDownloads removeObjectForKey: [[download request] URL]];
    if ([fPendingTorrentDownloads count] == 0)
    {
        [fPendingTorrentDownloads release];
        fPendingTorrentDownloads = nil;
    }
    
    [download release];
}

- (void) downloadDidFinish: (NSURLDownload *) download
{
    NSString * path = [[fPendingTorrentDownloads objectForKey: [[download request] URL]] objectForKey: @"Path"];
    
    [self openFiles: [NSArray arrayWithObject: path] addType: ADD_URL forcePath: nil];
    
    //delete the torrent file after opening
    [[NSFileManager defaultManager] removeItemAtPath: path error: NULL];
    
    [fPendingTorrentDownloads removeObjectForKey: [[download request] URL]];
    if ([fPendingTorrentDownloads count] == 0)
    {
        [fPendingTorrentDownloads release];
        fPendingTorrentDownloads = nil;
    }
    
    [download release];
}

- (void) application: (NSApplication *) app openFiles: (NSArray *) filenames
{
    [self openFiles: filenames addType: ADD_MANUAL forcePath: nil];
}

- (void) openFiles: (NSArray *) filenames addType: (addType) type forcePath: (NSString *) path
{
    #warning checks could probably be removed, since location is checked when starting
    if (!path && [fDefaults boolForKey: @"UseIncompleteDownloadFolder"]
        && access([[[fDefaults stringForKey: @"IncompleteDownloadFolder"] stringByExpandingTildeInPath] UTF8String], 0))
    {
        NSOpenPanel * panel = [NSOpenPanel openPanel];
        
        [panel setPrompt: NSLocalizedString(@"Select", "Default incomplete folder cannot be used alert -> prompt")];
        [panel setAllowsMultipleSelection: NO];
        [panel setCanChooseFiles: NO];
        [panel setCanChooseDirectories: YES];
        [panel setCanCreateDirectories: YES];

        [panel setMessage: NSLocalizedString(@"The incomplete folder cannot be used. Choose a new location or cancel for none.",
                                        "Default incomplete folder cannot be used alert -> message")];
        
        NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: filenames, @"Filenames",
                                                        [NSNumber numberWithInt: type], @"AddType", nil];
        
        [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: fWindow modalDelegate: self
                didEndSelector: @selector(incompleteChoiceClosed:returnCode:contextInfo:) contextInfo: dict];
        return;
    }
    
    if (!path && [fDefaults boolForKey: @"DownloadLocationConstant"]
        && access([[[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath] UTF8String], 0))
    {
        NSOpenPanel * panel = [NSOpenPanel openPanel];
        
        [panel setPrompt: NSLocalizedString(@"Select", "Default folder cannot be used alert -> prompt")];
        [panel setAllowsMultipleSelection: NO];
        [panel setCanChooseFiles: NO];
        [panel setCanChooseDirectories: YES];
        [panel setCanCreateDirectories: YES];

        [panel setMessage: NSLocalizedString(@"The download folder cannot be used. Choose a new location.",
                                        "Default folder cannot be used alert -> message")];
        
        NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: filenames, @"Filenames",
                                                        [NSNumber numberWithInt: type], @"AddType", nil];
        
        [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: fWindow modalDelegate: self
                didEndSelector: @selector(downloadChoiceClosed:returnCode:contextInfo:) contextInfo: dict];
        return;
    }
    
    torrentFileState deleteTorrentFile;
    switch (type)
    {
        case ADD_CREATED:
            deleteTorrentFile = TORRENT_FILE_SAVE;
            break;
        case ADD_URL:
            deleteTorrentFile = TORRENT_FILE_DELETE;
            break;
        default:
            deleteTorrentFile = TORRENT_FILE_DEFAULT;
    }
    
    tr_info info;
    for (NSString * torrentPath in filenames)
    {
        //ensure torrent doesn't already exist
        tr_ctor * ctor = tr_ctorNew(fLib);
        tr_ctorSetMetainfoFromFile(ctor, [torrentPath UTF8String]);
        int result = tr_torrentParse(fLib, ctor, &info);
        if (result != TR_OK)
        {
            if (result == TR_EDUPLICATE)
                [self duplicateOpenAlert: [NSString stringWithUTF8String: info.name]];
            else if (result == TR_EINVALID)
            {
                if (type != ADD_AUTO)
                    [self invalidOpenAlert: [torrentPath lastPathComponent]];
            }
            else //this shouldn't happen
                NSLog(@"Unknown error code (%d) when attempting to open \"%@\"", result, torrentPath);
            
            tr_ctorFree(ctor);
            tr_metainfoFree(&info);
            continue;
        }
        
        tr_ctorFree(ctor);
        
        //determine download location
        NSString * location;
        BOOL lockDestination = NO; //don't override the location with a group location if it has a hardcoded path
        if (path)
        {
            location = [path stringByExpandingTildeInPath];
            lockDestination = YES;
        }
        else if ([fDefaults boolForKey: @"DownloadLocationConstant"])
            location = [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath];
        else if (type != ADD_URL)
            location = [torrentPath stringByDeletingLastPathComponent];
        else
            location = nil;
        
        //determine to show the options window
        BOOL showWindow = type == ADD_SHOW_OPTIONS || ([fDefaults boolForKey: @"DownloadAsk"]
                            && (info.isMultifile || ![fDefaults boolForKey: @"DownloadAskMulti"])
                            && (type != ADD_AUTO || ![fDefaults boolForKey: @"DownloadAskManual"]));
        tr_metainfoFree(&info);
        
        Torrent * torrent;
        if (!(torrent = [[Torrent alloc] initWithPath: torrentPath location: location
                            deleteTorrentFile: showWindow ? TORRENT_FILE_SAVE : deleteTorrentFile lib: fLib]))
            continue;
        
        //change the location if the group calls for it (this has to wait until after the torrent is create)
        if (!lockDestination && [[GroupsController groups] usesCustomDownloadLocationForIndex: [torrent groupValue]])
        {
            location = [[GroupsController groups] customDownloadLocationForIndex: [torrent groupValue]];
            [torrent changeDownloadFolder: location];
        }
        
        //verify the data right away if it was newly created
        if (type == ADD_CREATED)
            [torrent resetCache];
        
        //add it to the "File -> Open Recent" menu
        [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];
        
        //show the add window or add directly
        if (showWindow || !location)
        {
            AddWindowController * addController = [[AddWindowController alloc] initWithTorrent: torrent destination: location
                                                    lockDestination: lockDestination controller: self deleteTorrent: deleteTorrentFile];
            [addController showWindow: self];
        }
        else
        {
            [torrent setWaitToStart: [fDefaults boolForKey: @"AutoStartDownload"]];
            
            [torrent update];
            [fTorrents addObject: torrent];
            [torrent release];
        }
    }

    [self updateTorrentsInQueue];
}

- (void) askOpenConfirmed: (AddWindowController *) addController add: (BOOL) add
{
    Torrent * torrent = [addController torrent];
    [addController release];
    
    if (add)
    {
        [torrent update];
        [fTorrents addObject: torrent];
        [torrent release];
        
        [self updateTorrentsInQueue];
    }
    else
    {
        [torrent closeRemoveTorrent];
        [torrent release];
    }
}

- (void) openCreatedFile: (NSNotification *) notification
{
    NSDictionary * dict = [notification userInfo];
    [self openFiles: [NSArray arrayWithObject: [dict objectForKey: @"File"]] addType: ADD_CREATED
                        forcePath: [dict objectForKey: @"Path"]];
    [dict release];
}

- (void) incompleteChoiceClosed: (NSOpenPanel *) openPanel returnCode: (NSInteger) code contextInfo: (NSDictionary *) dictionary
{
    if (code == NSOKButton)
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"IncompleteDownloadFolder"];
    else
        [fDefaults setBool: NO forKey: @"UseIncompleteDownloadFolder"];
    
    [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dictionary waitUntilDone: NO];
}

- (void) downloadChoiceClosed: (NSOpenPanel *) openPanel returnCode: (NSInteger) code contextInfo: (NSDictionary *) dictionary
{
    if (code == NSOKButton)
    {
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"DownloadFolder"];
        [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dictionary waitUntilDone: NO];
    }
    else
        [dictionary release];
}

- (void) openFilesWithDict: (NSDictionary *) dictionary
{
    [self openFiles: [dictionary objectForKey: @"Filenames"] addType: [[dictionary objectForKey: @"AddType"] intValue] forcePath: nil];
    
    [dictionary release];
}

//called on by applescript
- (void) open: (NSArray *) files
{
    NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: files, @"Filenames",
                                [NSNumber numberWithInt: ADD_MANUAL], @"AddType", nil];
    [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dict waitUntilDone: NO];
}

- (void) openShowSheet: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setAllowsMultipleSelection: YES];
    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];

    [panel beginSheetForDirectory: nil file: nil types: [NSArray arrayWithObject: @"org.bittorrent.torrent"]
        modalForWindow: fWindow modalDelegate: self didEndSelector: @selector(openSheetClosed:returnCode:contextInfo:)
        contextInfo: [NSNumber numberWithBool: sender == fOpenIgnoreDownloadFolder]];
}

- (void) openSheetClosed: (NSOpenPanel *) panel returnCode: (NSInteger) code contextInfo: (NSNumber *) useOptions
{
    if (code == NSOKButton)
    {
        NSDictionary * dictionary = [[NSDictionary alloc] initWithObjectsAndKeys: [panel filenames], @"Filenames",
            [NSNumber numberWithInt: [useOptions boolValue] ? ADD_SHOW_OPTIONS : ADD_MANUAL], @"AddType", nil];
        [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dictionary waitUntilDone: NO];
    }
}

- (void) invalidOpenAlert: (NSString *) filename
{
    if (![fDefaults boolForKey: @"WarningInvalidOpen"])
        return;
    
    NSAlert * alert = [[NSAlert alloc] init];
    [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"\"%@\" is not a valid torrent file.",
                            "Open invalid alert -> title"), filename]];
    [alert setInformativeText:
            NSLocalizedString(@"The torrent file cannot be opened because it contains invalid data.",
                            "Open invalid alert -> message")];
    [alert setAlertStyle: NSWarningAlertStyle];
    [alert addButtonWithTitle: NSLocalizedString(@"OK", "Open invalid alert -> button")];
    
    [alert runModal];
    if ([[alert suppressionButton] state] == NSOnState)
        [fDefaults setBool: NO forKey: @"WarningInvalidOpen"];
    [alert release];
}

- (void) duplicateOpenAlert: (NSString *) name
{
    if (![fDefaults boolForKey: @"WarningDuplicate"])
        return;
    
    NSAlert * alert = [[NSAlert alloc] init];
    [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"A transfer of \"%@\" already exists.",
                            "Open duplicate alert -> title"), name]];
    [alert setInformativeText:
            NSLocalizedString(@"The torrent file cannot be opened because it is a duplicate of an already added transfer.",
                            "Open duplicate alert -> message")];
    [alert setAlertStyle: NSWarningAlertStyle];
    [alert addButtonWithTitle: NSLocalizedString(@"OK", "Open duplicate alert -> button")];
    [alert setShowsSuppressionButton: YES];
    
    [alert runModal];
    if ([[alert suppressionButton] state])
        [fDefaults setBool: NO forKey: @"WarningDuplicate"];
    [alert release];
}

- (void) openURL: (NSURL *) url
{
    [[NSURLDownload alloc] initWithRequest: [NSURLRequest requestWithURL: url] delegate: self];
}

- (void) openURLShowSheet: (id) sender
{
    [NSApp beginSheet: fURLSheetWindow modalForWindow: fWindow modalDelegate: self
            didEndSelector: @selector(urlSheetDidEnd:returnCode:contextInfo:) contextInfo: nil];
}

- (void) openURLEndSheet: (id) sender
{
    [fURLSheetWindow orderOut: sender];
    [NSApp endSheet: fURLSheetWindow returnCode: 1];
}

- (void) openURLCancelEndSheet: (id) sender
{
    [fURLSheetWindow orderOut: sender];
    [NSApp endSheet: fURLSheetWindow returnCode: 0];
}

- (void) controlTextDidChange: (NSNotification *) notification
{
    if ([notification object] != fURLSheetTextField)
        return;
    
    NSString * string = [fURLSheetTextField stringValue];
    BOOL enable = YES;
    if ([string isEqualToString: @""])
        enable = NO;
    else
    {
        NSRange prefixRange = [string rangeOfString: @"://"];
        if (prefixRange.location != NSNotFound && [string length] == NSMaxRange(prefixRange))
            enable = NO;
    }
    
    [fURLSheetOpenButton setEnabled: enable];
}

- (void) urlSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo
{
    [fURLSheetTextField selectText: self];
    if (returnCode != 1)
        return;
    
    NSString * urlString = [fURLSheetTextField stringValue];
    if ([urlString rangeOfString: @"://"].location == NSNotFound)
    {
        if ([urlString rangeOfString: @"."].location == NSNotFound)
        {
            NSInteger beforeCom;
            if ((beforeCom = [urlString rangeOfString: @"/"].location) != NSNotFound)
                urlString = [NSString stringWithFormat: @"http://www.%@.com/%@",
                                [urlString substringToIndex: beforeCom],
                                [urlString substringFromIndex: beforeCom + 1]];
            else
                urlString = [NSString stringWithFormat: @"http://www.%@.com/", urlString];
        }
        else
            urlString = [@"http://" stringByAppendingString: urlString];
    }
    
    NSURL * url = [NSURL URLWithString: urlString];
    [self performSelectorOnMainThread: @selector(openURL:) withObject: url waitUntilDone: NO];
}

- (void) createFile: (id) sender
{
    [CreatorWindowController createTorrentFile: fLib];
}

- (void) resumeSelectedTorrents: (id) sender
{
    [self resumeTorrents: [fTableView selectedTorrents]];
}

- (void) resumeAllTorrents: (id) sender
{
    [self resumeTorrents: fTorrents];
}

- (void) resumeTorrents: (NSArray *) torrents
{
    for (Torrent * torrent in torrents)
        [torrent setWaitToStart: YES];
    
    [self updateTorrentsInQueue];
}

- (void) resumeSelectedTorrentsNoWait:  (id) sender
{
    [self resumeTorrentsNoWait: [fTableView selectedTorrents]];
}

- (void) resumeWaitingTorrents: (id) sender
{
    NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [fTorrents count]];
    
    for (Torrent * torrent in fTorrents)
        if (![torrent isActive] && [torrent waitingToStart])
            [torrents addObject: torrent];
    
    [self resumeTorrentsNoWait: torrents];
}

- (void) resumeTorrentsNoWait: (NSArray *) torrents
{
    //iterate through instead of all at once to ensure no conflicts
    for (Torrent * torrent in torrents)
        [torrent startTransfer];
    
    [self updateUI];
    [self applyFilter: nil];
    [self updateTorrentHistory];
}

- (void) stopSelectedTorrents: (id) sender
{
    [self stopTorrents: [fTableView selectedTorrents]];
}

- (void) stopAllTorrents: (id) sender
{
    [self stopTorrents: fTorrents];
}

- (void) stopTorrents: (NSArray *) torrents
{
    //don't want any of these starting then stopping
    for (Torrent * torrent in torrents)
        [torrent setWaitToStart: NO];

    [torrents makeObjectsPerformSelector: @selector(stopTransfer)];
    
    [self updateUI];
    [self applyFilter: nil];
    [self updateTorrentHistory];
}

- (void) removeTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    [torrents retain];
    NSInteger active = 0, downloading = 0;

    if ([fDefaults boolForKey: @"CheckRemove"])
    {
        for (Torrent * torrent in torrents)
            if ([torrent isActive])
            {
                active++;
                if (![torrent isSeeding])
                    downloading++;
            }

        if ([fDefaults boolForKey: @"CheckRemoveDownloading"] ? downloading > 0 : active > 0)
        {
            NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                                    torrents, @"Torrents",
                                    [NSNumber numberWithBool: deleteData], @"DeleteData",
                                    [NSNumber numberWithBool: deleteTorrent], @"DeleteTorrent", nil];
            
            NSString * title, * message;
            
            NSInteger selected = [torrents count];
            if (selected == 1)
            {
                NSString * torrentName = [[torrents objectAtIndex: 0] name];
                
                if (!deleteData && !deleteTorrent)
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list?",
                                "Removal confirm panel -> title"), torrentName];
                else if (deleteData && !deleteTorrent)
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list"
                                " and trash the data file?", "Removal confirm panel -> title"), torrentName];
                else if (!deleteData && deleteTorrent)
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list"
                                " and trash the torrent file?", "Removal confirm panel -> title"), torrentName];
                else
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list"
                                " and trash both the data and torrent files?", "Removal confirm panel -> title"), torrentName];
                
                message = NSLocalizedString(@"This transfer is active."
                            " Once removed, continuing the transfer will require the torrent file.",
                            "Removal confirm panel -> message");
            }
            else
            {
                if (!deleteData && !deleteTorrent)
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove %d transfers from the transfer list?",
                                "Removal confirm panel -> title"), selected];
                else if (deleteData && !deleteTorrent)
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove %d transfers from the transfer list"
                                " and trash the data file?", "Removal confirm panel -> title"), selected];
                else if (!deleteData && deleteTorrent)
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove %d transfers from the transfer list"
                                " and trash the torrent file?", "Removal confirm panel -> title"), selected];
                else
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove %d transfers from the transfer list"
                                " and trash both the data and torrent files?", "Removal confirm panel -> title"), selected];
                
                if (selected == active)
                    message = [NSString stringWithFormat: NSLocalizedString(@"There are %d active transfers.",
                                "Removal confirm panel -> message part 1"), active];
                else
                    message = [NSString stringWithFormat: NSLocalizedString(@"There are %d transfers (%d active).",
                                "Removal confirm panel -> message part 1"), selected, active];
                message = [message stringByAppendingFormat: @" %@",
                                NSLocalizedString(@"Once removed, continuing the transfers will require the torrent files.",
                                "Removal confirm panel -> message part 2")];
            }
            
            NSBeginAlertSheet(title, NSLocalizedString(@"Remove", "Removal confirm panel -> button"),
                NSLocalizedString(@"Cancel", "Removal confirm panel -> button"), nil, fWindow, self,
                nil, @selector(removeSheetDidEnd:returnCode:contextInfo:), dict, message);
            return;
        }
    }
    
    [self confirmRemoveTorrents: torrents deleteData: deleteData deleteTorrent: deleteTorrent];
}

- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (NSDictionary *) dict
{
    NSArray * torrents = [dict objectForKey: @"Torrents"];
    if (returnCode == NSAlertDefaultReturn)
        [self confirmRemoveTorrents: torrents deleteData: [[dict objectForKey: @"DeleteData"] boolValue]
                deleteTorrent: [[dict objectForKey: @"DeleteTorrent"] boolValue]];
    else
        [torrents release];
    
    [dict release];
}

- (void) confirmRemoveTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    //don't want any of these starting then stopping
    for (Torrent * torrent in torrents)
        [torrent setWaitToStart: NO];
    
    [fTorrents removeObjectsInArray: torrents];
    
    for (Torrent * torrent in torrents)
    {
        //let's expand all groups that have removed items - they either don't exist anymore, are already expanded, or are collapsed (rpc)
        [fTableView removeCollapsedGroup: [torrent groupValue]];
        
        if (deleteData)
            [torrent trashData];
        if (deleteTorrent)
            [torrent trashTorrent];
        
        [torrent closeRemoveTorrent];
    }
    
    [torrents release];
    
    [fTableView deselectAll: nil];
    
    [self updateTorrentsInQueue];
}

- (void) removeNoDelete: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents] deleteData: NO deleteTorrent: NO];
}

- (void) removeDeleteData: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents] deleteData: YES deleteTorrent: NO];
}

- (void) removeDeleteTorrent: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents] deleteData: NO deleteTorrent: YES];
}

- (void) removeDeleteDataAndTorrent: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents] deleteData: YES deleteTorrent: YES];
}

- (void) moveDataFilesSelected: (id) sender
{
    [self moveDataFiles: [fTableView selectedTorrents]];
}

- (void) moveDataFiles: (NSArray *) torrents
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];
    [panel setPrompt: NSLocalizedString(@"Select", "Move torrent -> prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];
    
    torrents = [torrents retain];
    NSInteger count = [torrents count];
    if (count == 1)
        [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the new folder for \"%@\".",
                            "Move torrent -> select destination folder"), [[torrents objectAtIndex: 0] name]]];
    else
        [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the new folder for %d data files.",
                            "Move torrent -> select destination folder"), count]];
        
    [panel beginSheetForDirectory: nil file: nil modalForWindow: fWindow modalDelegate: self
        didEndSelector: @selector(moveDataFileChoiceClosed:returnCode:contextInfo:) contextInfo: torrents];
}

- (void) moveDataFileChoiceClosed: (NSOpenPanel *) panel returnCode: (NSInteger) code contextInfo: (NSArray *) torrents
{
    if (code == NSOKButton)
    {
        for (Torrent * torrent in torrents)
            [torrent moveTorrentDataFileTo: [[panel filenames] objectAtIndex: 0]];
    }
    
    [torrents release];
}

- (void) copyTorrentFiles: (id) sender
{
    [self copyTorrentFileForTorrents: [[NSMutableArray alloc] initWithArray: [fTableView selectedTorrents]]];
}

- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents
{
    if ([torrents count] <= 0)
    {
        [torrents release];
        return;
    }

    Torrent * torrent = [torrents objectAtIndex: 0];

    //warn user if torrent file can't be found
    if (![[NSFileManager defaultManager] fileExistsAtPath: [torrent torrentLocation]])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Torrent file copy alert -> button")];
        [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"Copy of \"%@\" Cannot Be Created",
                                "Torrent file copy alert -> title"), [torrent name]]];
        [alert setInformativeText: [NSString stringWithFormat: 
                NSLocalizedString(@"The torrent file (%@) cannot be found.", "Torrent file copy alert -> message"),
                                    [torrent torrentLocation]]];
        [alert setAlertStyle: NSWarningAlertStyle];
        
        [alert runModal];
        [alert release];
        
        [torrents removeObjectAtIndex: 0];
        [self copyTorrentFileForTorrents: torrents];
    }
    else
    {
        NSSavePanel * panel = [NSSavePanel savePanel];
        [panel setRequiredFileType: @"org.bittorrent.torrent"];
        [panel setCanSelectHiddenExtension: YES];
        
        [panel beginSheetForDirectory: nil file: [torrent name] modalForWindow: fWindow modalDelegate: self
            didEndSelector: @selector(saveTorrentCopySheetClosed:returnCode:contextInfo:) contextInfo: torrents];
    }
}

- (void) saveTorrentCopySheetClosed: (NSSavePanel *) panel returnCode: (NSInteger) code contextInfo: (NSMutableArray *) torrents
{
    //copy torrent to new location with name of data file
    if (code == NSOKButton)
        [[torrents objectAtIndex: 0] copyTorrentFileTo: [panel filename]];
    
    [torrents removeObjectAtIndex: 0];
    [self performSelectorOnMainThread: @selector(copyTorrentFileForTorrents:) withObject: torrents waitUntilDone: NO];
}

- (void) revealFile: (id) sender
{
    for (Torrent * torrent in [fTableView selectedTorrents])
        [torrent revealData];
}

- (void) announceSelectedTorrents: (id) sender
{
    for (Torrent * torrent in [fTableView selectedTorrents])
    {
        if ([torrent canManualAnnounce])
            [torrent manualAnnounce];
    }
}

- (void) verifySelectedTorrents: (id) sender
{
    [self verifyTorrents: [fTableView selectedTorrents]];
}

- (void) verifyTorrents: (NSArray *) torrents
{
    for (Torrent * torrent in torrents)
        [torrent resetCache];
    
    [self applyFilter: nil];
}

- (void) showPreferenceWindow: (id) sender
{
    NSWindow * window = [fPrefsController window];
    if (![window isVisible])
        [window center];

    [window makeKeyAndOrderFront: nil];
}

- (void) showAboutWindow: (id) sender
{
    [[AboutWindowController aboutController] showWindow: nil];
}

- (void) showInfo: (id) sender
{
    if ([[fInfoController window] isVisible])
        [fInfoController close];
    else
    {
        [fInfoController updateInfoStats];
        [[fInfoController window] orderFront: nil];
    }
}

- (void) resetInfo
{
    [fInfoController setInfoForTorrents: [fTableView selectedTorrents]];
    [[QuickLookController quickLook] updateQuickLook];
}

- (void) setInfoTab: (id) sender
{
    if (sender == fNextInfoTabItem)
        [fInfoController setNextTab];
    else
        [fInfoController setPreviousTab];
}

- (void) showMessageWindow: (id) sender
{
    [fMessageController showWindow: nil];
}

- (void) showStatsWindow: (id) sender
{
    [[StatsWindowController statsWindow: fLib] showWindow: nil];
}

- (void) updateUI
{
    [fTorrents makeObjectsPerformSelector: @selector(update)];
    
    if (![NSApp isHidden])
    {
        if ([fWindow isVisible])
        {
            [self sortTorrents];
            
            //update status bar
            if (![fStatusBar isHidden])
            {
                //set rates
                [fTotalDLField setStringValue: [NSString stringForSpeed: tr_sessionGetPieceSpeed(fLib, TR_DOWN)]];
                [fTotalULField setStringValue: [NSString stringForSpeed: tr_sessionGetPieceSpeed(fLib, TR_UP)]];
                
                //set status button text
                NSString * statusLabel = [fDefaults stringForKey: @"StatusLabel"], * statusString;
                BOOL total;
                if ((total = [statusLabel isEqualToString: STATUS_RATIO_TOTAL]) || [statusLabel isEqualToString: STATUS_RATIO_SESSION])
                {
                    tr_session_stats stats;
                    if (total)
                        tr_sessionGetCumulativeStats(fLib, &stats);
                    else
                        tr_sessionGetStats(fLib, &stats);
                    
                    statusString = [NSLocalizedString(@"Ratio", "status bar -> status label") stringByAppendingFormat: @": %@",
                                    [NSString stringForRatio: stats.ratio]];
                }
                else //STATUS_TRANSFER_TOTAL or STATUS_TRANSFER_SESSION
                {
                    total = [statusLabel isEqualToString: STATUS_TRANSFER_TOTAL];
                    
                    tr_session_stats stats;
                    if (total)
                        tr_sessionGetCumulativeStats(fLib, &stats);
                    else
                        tr_sessionGetStats(fLib, &stats);
                    
                    statusString = [NSString stringWithFormat: @"%@: %@  %@: %@",
                            NSLocalizedString(@"DL", "status bar -> status label"), [NSString stringForFileSize: stats.downloadedBytes],
                            NSLocalizedString(@"UL", "status bar -> status label"), [NSString stringForFileSize: stats.uploadedBytes]];
                }
                
                [fStatusButton setTitle: statusString];
                [self resizeStatusButton];
            }
        }

        //update non-constant parts of info window
        if ([[fInfoController window] isVisible])
            [fInfoController updateInfoStats];
    }
    
    //badge dock
    [fBadger updateBadge];
}

- (void) resizeStatusButton
{
    [fStatusButton sizeToFit];
    
    //width ends up being too long
    NSRect statusFrame = [fStatusButton frame];
    statusFrame.size.width -= 25.0;
    
    CGFloat difference = NSMaxX(statusFrame) + 5.0 - [fTotalDLImageView frame].origin.x;
    if (difference > 0)
        statusFrame.size.width -= difference;
    
    [fStatusButton setFrame: statusFrame];
}

- (void) setBottomCountText: (BOOL) filtering
{
    NSString * totalTorrentsString;
    NSInteger totalCount = [fTorrents count];
    if (totalCount != 1)
        totalTorrentsString = [NSString stringWithFormat: NSLocalizedString(@"%d transfers", "Status bar transfer count"), totalCount];
    else
        totalTorrentsString = NSLocalizedString(@"1 transfer", "Status bar transfer count");
    
    if (filtering)
    {
        NSInteger count = [fTableView numberOfRows]; //have to factor in collapsed rows
        if (count > 0 && ![[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [Torrent class]])
            count -= [fDisplayedTorrents count];
        
        totalTorrentsString = [NSString stringWithFormat: NSLocalizedString(@"%d of %@", "Status bar transfer count"),
                                count, totalTorrentsString];
    }
    
    [fTotalTorrentsField setStringValue: totalTorrentsString];
}

- (void) updateSpeedFieldsToolTips
{
    NSString * uploadText, * downloadText;
    
    if ([fDefaults boolForKey: @"SpeedLimit"])
    {
        NSString * speedString = [NSString stringWithFormat: @"%@ (%@)", NSLocalizedString(@"%d KB/s", "Status Bar -> speed tooltip"),
                                    NSLocalizedString(@"Speed Limit", "Status Bar -> speed tooltip")];
        
        uploadText = [NSString stringWithFormat: speedString, [fDefaults integerForKey: @"SpeedLimitUploadLimit"]];
        downloadText = [NSString stringWithFormat: speedString, [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]];
    }
    else
    {
        if ([fDefaults boolForKey: @"CheckUpload"])
            uploadText = [NSString stringWithFormat: NSLocalizedString(@"%d KB/s", "Status Bar -> speed tooltip"),
                            [fDefaults integerForKey: @"UploadLimit"]];
        else
            uploadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
        
        if ([fDefaults boolForKey: @"CheckDownload"])
            downloadText = [NSString stringWithFormat: NSLocalizedString(@"%d KB/s", "Status Bar -> speed tooltip"),
                            [fDefaults integerForKey: @"DownloadLimit"]];
        else
            downloadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
    }
    
    uploadText = [NSLocalizedString(@"Total upload rate", "Status Bar -> speed tooltip")
                    stringByAppendingFormat: @": %@", uploadText];
    downloadText = [NSLocalizedString(@"Total download rate", "Status Bar -> speed tooltip")
                    stringByAppendingFormat: @": %@", downloadText];
    
    [fTotalULField setToolTip: uploadText];
    [fTotalDLField setToolTip: downloadText];
}

- (void) updateTorrentsInQueue
{
    BOOL download = [fDefaults boolForKey: @"Queue"],
        seed = [fDefaults boolForKey: @"QueueSeed"];
    
    NSInteger desiredDownloadActive = [self numToStartFromQueue: YES],
        desiredSeedActive = [self numToStartFromQueue: NO];
    
    for (Torrent * torrent in fTorrents)
    {
        if (![torrent isActive] && ![torrent isChecking] && [torrent waitingToStart])
        {
            if (![torrent allDownloaded])
            {
                if (!download || desiredDownloadActive > 0)
                {
                    [torrent startTransfer];
                    if ([torrent isActive])
                        desiredDownloadActive--;
                    [torrent update];
                }
            }
            else
            {
                if (!seed || desiredSeedActive > 0)
                {
                    [torrent startTransfer];
                    if ([torrent isActive])
                        desiredSeedActive--;
                    [torrent update];
                }
            }
        }
    }
    
    [self updateUI];
    [self applyFilter: nil];
    [self updateTorrentHistory];
}

- (NSInteger) numToStartFromQueue: (BOOL) downloadQueue
{
    if (![fDefaults boolForKey: downloadQueue ? @"Queue" : @"QueueSeed"])
        return 0;
    
    NSInteger desired = [fDefaults integerForKey: downloadQueue ? @"QueueDownloadNumber" : @"QueueSeedNumber"];
        
    for (Torrent * torrent in fTorrents)
    {
        if ([torrent isChecking])
        {
            desired--;
            if (desired <= 0)
                return 0;
        }
        else if ([torrent isActive] && ![torrent isStalled] && ![torrent isError])
        {
            if ([torrent allDownloaded] != downloadQueue)
            {
                desired--;
                if (desired <= 0)
                    return 0;
            }
        }
        else;
    }
    
    return desired;
}

- (void) torrentFinishedDownloading: (NSNotification *) notification
{
    Torrent * torrent = [notification object];
    
    if ([torrent isActive])
    {
        if (!fSoundPlaying && [fDefaults boolForKey: @"PlayDownloadSound"])
        {
            NSSound * sound;
            if ((sound = [NSSound soundNamed: [fDefaults stringForKey: @"DownloadSound"]]))
            {
                [sound setDelegate: self];
                fSoundPlaying = YES;
                [sound play];
            }
        }
        
        NSDictionary * clickContext = [NSDictionary dictionaryWithObjectsAndKeys: GROWL_DOWNLOAD_COMPLETE, @"Type",
                                        [torrent dataLocation] , @"Location", nil];
        [GrowlApplicationBridge notifyWithTitle: NSLocalizedString(@"Download Complete", "Growl notification title")
                                    description: [torrent name] notificationName: GROWL_DOWNLOAD_COMPLETE
                                    iconData: nil priority: 0 isSticky: NO clickContext: clickContext];
        
        if (![fWindow isMainWindow])
            [fBadger incrementCompleted];
        
        if ([fDefaults boolForKey: @"QueueSeed"] && [self numToStartFromQueue: NO] <= 0)
        {
            [torrent stopTransfer];
            [torrent setWaitToStart: YES];
        }
    }
    
    [self updateTorrentsInQueue];
}

- (void) torrentRestartedDownloading: (NSNotification *) notification
{
    Torrent * torrent = [notification object];
    if ([torrent isActive])
    {
        if ([fDefaults boolForKey: @"Queue"] && [self numToStartFromQueue: YES] <= 0)
        {
            [torrent stopTransfer];
            [torrent setWaitToStart: YES];
        }
    }
    
    [self updateTorrentsInQueue];
}

- (void) torrentStoppedForRatio: (NSNotification *) notification
{
    Torrent * torrent = [notification object];
    
    [self updateTorrentsInQueue];
    
    if ([[fTableView selectedTorrents] containsObject: torrent])
    {
        [fInfoController updateInfoStats];
        [fInfoController updateOptions];
    }
    
    if (!fSoundPlaying && [fDefaults boolForKey: @"PlaySeedingSound"])
    {
        NSSound * sound;
        if ((sound = [NSSound soundNamed: [fDefaults stringForKey: @"SeedingSound"]]))
        {
            [sound setDelegate: self];
            fSoundPlaying = YES;
            [sound play];
        }
    }
    
    NSDictionary * clickContext = [NSDictionary dictionaryWithObjectsAndKeys: GROWL_SEEDING_COMPLETE, @"Type",
                                    [torrent dataLocation], @"Location", nil];
    [GrowlApplicationBridge notifyWithTitle: NSLocalizedString(@"Seeding Complete", "Growl notification title")
                        description: [torrent name] notificationName: GROWL_SEEDING_COMPLETE
                        iconData: nil priority: 0 isSticky: NO clickContext: clickContext];
}

- (void) updateTorrentHistory
{
    NSMutableArray * history = [NSMutableArray arrayWithCapacity: [fTorrents count]];
    
    for (Torrent * torrent in fTorrents)
        [history addObject: [torrent history]];
    
    [history writeToFile: [NSHomeDirectory() stringByAppendingPathComponent: SUPPORT_FOLDER] atomically: YES];
}

- (void) setSort: (id) sender
{
    NSString * sortType;
    switch ([sender tag])
    {
        case SORT_ORDER_TAG:
            sortType = SORT_ORDER;
            [fDefaults setBool: NO forKey: @"SortReverse"];
            break;
        case SORT_DATE_TAG:
            sortType = SORT_DATE;
            break;
        case SORT_NAME_TAG:
            sortType = SORT_NAME;
            break;
        case SORT_PROGRESS_TAG:
            sortType = SORT_PROGRESS;
            break;
        case SORT_STATE_TAG:
            sortType = SORT_STATE;
            break;
        case SORT_TRACKER_TAG:
            sortType = SORT_TRACKER;
            break;
        case SORT_ACTIVITY_TAG:
            sortType = SORT_ACTIVITY;
            break;
        default:
            return;
    }
    
    [fDefaults setObject: sortType forKey: @"Sort"];
    [self sortTorrents];
}

- (void) setSortByGroup: (id) sender
{
    BOOL sortByGroup = ![fDefaults boolForKey: @"SortByGroup"];
    [fDefaults setBool: sortByGroup forKey: @"SortByGroup"];
    
    //expand all groups
    if (sortByGroup)
        [fTableView removeAllCollapsedGroups];
    
    [self applyFilter: nil];
}

- (void) setSortReverse: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"SortReverse"] forKey: @"SortReverse"];
    [self sortTorrents];
}

- (void) sortTorrents
{
    NSArray * selectedValues = [fTableView selectedValues];
    
    [self sortTorrentsIgnoreSelected]; //actually sort
    
    [fTableView selectValues: selectedValues];
}

- (void) sortTorrentsIgnoreSelected
{
    NSString * sortType = [fDefaults stringForKey: @"Sort"];
    
    if (![sortType isEqualToString: SORT_ORDER])
    {
        const BOOL asc = ![fDefaults boolForKey: @"SortReverse"];
        
        NSArray * descriptors;
        NSSortDescriptor * nameDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"name" ascending: asc
                                                selector: @selector(compareFinder:)] autorelease];
        
        if ([sortType isEqualToString: SORT_STATE])
        {
            NSSortDescriptor * stateDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"stateSortKey" ascending: !asc] autorelease],
                            * progressDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"progress" ascending: !asc] autorelease],
                            * ratioDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"ratio" ascending: !asc] autorelease];
            
            descriptors = [[NSArray alloc] initWithObjects: stateDescriptor, progressDescriptor, ratioDescriptor,
                                                                nameDescriptor, nil];
        }
        else if ([sortType isEqualToString: SORT_PROGRESS])
        {
            NSSortDescriptor * progressDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"progress" ascending: asc] autorelease],
                            * ratioProgressDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"progressStopRatio"
                                                            ascending: asc] autorelease],
                            * ratioDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"ratio" ascending: asc] autorelease];
            
            descriptors = [[NSArray alloc] initWithObjects: progressDescriptor, ratioProgressDescriptor, ratioDescriptor,
                                                                nameDescriptor, nil];
        }
        else if ([sortType isEqualToString: SORT_TRACKER])
        {
            NSSortDescriptor * trackerDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"trackerAddressAnnounce" ascending: asc
                                                    selector: @selector(localizedCaseInsensitiveCompare:)] autorelease];
            
            descriptors = [[NSArray alloc] initWithObjects: trackerDescriptor, nameDescriptor, nil];
        }
        else if ([sortType isEqualToString: SORT_ACTIVITY])
        {
            NSSortDescriptor * rateDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"totalRate" ascending: !asc] autorelease];
            NSSortDescriptor * activityDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"dateActivityOrAdd" ascending: !asc]
                                                        autorelease];
            
            descriptors = [[NSArray alloc] initWithObjects: rateDescriptor, activityDescriptor, nameDescriptor, nil];
        }
        else if ([sortType isEqualToString: SORT_DATE])
        {
            NSSortDescriptor * dateDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"dateAdded" ascending: asc] autorelease];
        
            descriptors = [[NSArray alloc] initWithObjects: dateDescriptor, nameDescriptor, nil];
        }
        else
            descriptors = [[NSArray alloc] initWithObjects: nameDescriptor, nil];
        
        //actually sort
        if ([fDefaults boolForKey: @"SortByGroup"])
        {
            for (TorrentGroup * group in fDisplayedTorrents)
                [[group torrents] sortUsingDescriptors: descriptors];
        }
        else
            [fDisplayedTorrents sortUsingDescriptors: descriptors];
        
        [descriptors release];
    }
    
    [fTableView reloadData];
}

- (void) applyFilter: (id) sender
{
    //get all the torrents in the table
    NSMutableArray * previousTorrents;
    if ([fDisplayedTorrents count] > 0 && [[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [TorrentGroup class]])
    {
        previousTorrents = [NSMutableArray array];
        
        for (TorrentGroup * group in fDisplayedTorrents)
            [previousTorrents addObjectsFromArray: [group torrents]];
    }
    else
        previousTorrents = fDisplayedTorrents;
    
    NSArray * selectedValues = [fTableView selectedValues];
    
    NSUInteger active = 0, downloading = 0, seeding = 0, paused = 0;
    NSString * filterType = [fDefaults stringForKey: @"Filter"];
    BOOL filterActive = NO, filterDownload = NO, filterSeed = NO, filterPause = NO, filterStatus = YES;
    if ([filterType isEqualToString: FILTER_ACTIVE])
        filterActive = YES;
    else if ([filterType isEqualToString: FILTER_DOWNLOAD])
        filterDownload = YES;
    else if ([filterType isEqualToString: FILTER_SEED])
        filterSeed = YES;
    else if ([filterType isEqualToString: FILTER_PAUSE])
        filterPause = YES;
    else
        filterStatus = NO;
    
    const NSInteger groupFilterValue = [fDefaults integerForKey: @"FilterGroup"];
    const BOOL filterGroup = groupFilterValue != GROUP_FILTER_ALL_TAG;
    
    NSString * searchString = [fSearchFilterField stringValue];
    const BOOL filterText = [searchString length] > 0,
            filterTracker = filterText && [[fDefaults stringForKey: @"FilterSearchType"] isEqualToString: FILTER_TYPE_TRACKER];
    
    NSMutableArray * allTorrents = [NSMutableArray arrayWithCapacity: [fTorrents count]];
    
    //get count of each type
    for (Torrent * torrent in fTorrents)
    {
        //check status
        if ([torrent isActive] && ![torrent isCheckingWaiting])
        {
            if ([torrent isSeeding])
            {
                seeding++;
                BOOL isActive = ![torrent isStalled];
                if (isActive)
                    active++;
                
                if (filterStatus && !((filterActive && isActive) || filterSeed))
                    continue;
            }
            else
            {
                downloading++;
                BOOL isActive = ![torrent isStalled];
                if (isActive)
                    active++;
                
                if (filterStatus && !((filterActive && isActive) || filterDownload))
                    continue;
            }
        }
        else
        {
            paused++;
            if (filterStatus && !filterPause)
                continue;
        }
        
        //checkGroup
        if (filterGroup)
            if ([torrent groupValue] != groupFilterValue)
                continue;
        
        //check text field
        if (filterText)
        {
            if (filterTracker)
            {
                BOOL removeTextField = YES;
                for (NSString * tracker in [torrent allTrackers: NO])
                {
                    if ([tracker rangeOfString: searchString options: NSCaseInsensitiveSearch].location != NSNotFound)
                    {
                        removeTextField = NO;
                        break;
                    }
                }
                
                if (removeTextField)
                    continue;
            }
            else
            {
                if ([[torrent name] rangeOfString: searchString options: NSCaseInsensitiveSearch].location == NSNotFound)
                    continue;
            }
        }
        
        [allTorrents addObject: torrent];
    }
    
    //set button tooltips
    [fNoFilterButton setCount: [fTorrents count]];
    [fActiveFilterButton setCount: active];
    [fDownloadFilterButton setCount: downloading];
    [fSeedFilterButton setCount: seeding];
    [fPauseFilterButton setCount: paused];
    
    //clear display cache for not-shown torrents
    [previousTorrents removeObjectsInArray: allTorrents];
    for (Torrent * torrent in previousTorrents)
        [torrent setPreviousFinishedPieces: nil];
    
    //place torrents into groups
    const BOOL groupRows = [fDefaults boolForKey: @"SortByGroup"];
    if (groupRows)
    {
        NSMutableArray * oldTorrentGroups = [NSMutableArray array];
        if ([fDisplayedTorrents count] > 0 && [[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [TorrentGroup class]])
            [oldTorrentGroups addObjectsFromArray: fDisplayedTorrents];
        
        [fDisplayedTorrents removeAllObjects];
        
        NSSortDescriptor * groupDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"groupOrderValue" ascending: YES] autorelease];
        [allTorrents sortUsingDescriptors: [NSArray arrayWithObject: groupDescriptor]];
        
        NSMutableArray * groupTorrents;
        NSInteger lastGroupValue = -2, currentOldGroupIndex = 0;
        for (Torrent * torrent in allTorrents)
        {
            NSInteger groupValue = [torrent groupValue];
            if (groupValue != lastGroupValue)
            {
                lastGroupValue = groupValue;
                
                TorrentGroup * group = nil;
                
                //try to see if the group already exists
                for (; currentOldGroupIndex < [oldTorrentGroups count]; currentOldGroupIndex++)
                {
                    TorrentGroup * currentGroup = [oldTorrentGroups objectAtIndex: currentOldGroupIndex];
                    const NSInteger currentGroupValue = [currentGroup groupIndex];
                    if (currentGroupValue == groupValue)
                    {
                        group = currentGroup;
                        [[currentGroup torrents] removeAllObjects];
                        
                        currentOldGroupIndex++;
                    }
                    
                    if (currentGroupValue >= groupValue)
                        break;
                }
                
                if (!group)
                    group = [[[TorrentGroup alloc] initWithGroup: groupValue] autorelease];
                [fDisplayedTorrents addObject: group];
                
                groupTorrents = [group torrents];
            }
            
            [groupTorrents addObject: torrent];
        }
    }
    else
        [fDisplayedTorrents setArray: allTorrents];
    
    //actually sort
    [self sortTorrentsIgnoreSelected];
    
    //reset expanded/collapsed rows
    if (groupRows)
    {
        for (TorrentGroup * group in fDisplayedTorrents)
        {
            if ([fTableView isGroupCollapsed: [group groupIndex]])
                [fTableView collapseItem: group];
            else
                [fTableView expandItem: group];
        }
    }
    
    [fTableView selectValues: selectedValues];
    [self resetInfo]; //if group is already selected, but the torrents in it change
    
    [self setBottomCountText: groupRows || filterStatus || filterGroup || filterText];
    
    [self setWindowSizeToFit];
}

//resets filter and sorts torrents
- (void) setFilter: (id) sender
{
    NSString * oldFilterType = [fDefaults stringForKey: @"Filter"];
    
    NSButton * prevFilterButton;
    if ([oldFilterType isEqualToString: FILTER_PAUSE])
        prevFilterButton = fPauseFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_ACTIVE])
        prevFilterButton = fActiveFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_SEED])
        prevFilterButton = fSeedFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_DOWNLOAD])
        prevFilterButton = fDownloadFilterButton;
    else
        prevFilterButton = fNoFilterButton;
    
    if (sender != prevFilterButton)
    {
        [prevFilterButton setState: NSOffState];
        [sender setState: NSOnState];

        NSString * filterType;
        if (sender == fActiveFilterButton)
            filterType = FILTER_ACTIVE;
        else if (sender == fDownloadFilterButton)
            filterType = FILTER_DOWNLOAD;
        else if (sender == fPauseFilterButton)
            filterType = FILTER_PAUSE;
        else if (sender == fSeedFilterButton)
            filterType = FILTER_SEED;
        else
            filterType = FILTER_NONE;

        [fDefaults setObject: filterType forKey: @"Filter"];
    }
    else
        [sender setState: NSOnState];

    [self applyFilter: nil];
}

- (void) setFilterSearchType: (id) sender
{
    NSString * oldFilterType = [fDefaults stringForKey: @"FilterSearchType"];
    
    NSInteger prevTag, currentTag = [sender tag];
    if ([oldFilterType isEqualToString: FILTER_TYPE_TRACKER])
        prevTag = FILTER_TYPE_TAG_TRACKER;
    else
        prevTag = FILTER_TYPE_TAG_NAME;
    
    if (currentTag != prevTag)
    {
        NSString * filterType;
        if (currentTag == FILTER_TYPE_TAG_TRACKER)
            filterType = FILTER_TYPE_TRACKER;
        else
            filterType = FILTER_TYPE_NAME;
        
        [fDefaults setObject: filterType forKey: @"FilterSearchType"];
        
        [[fSearchFilterField cell] setPlaceholderString: [sender title]];
    }
    
    [self applyFilter: nil];
}

- (void) switchFilter: (id) sender
{
    NSString * filterType = [fDefaults stringForKey: @"Filter"];
    
    NSButton * button;
    if ([filterType isEqualToString: FILTER_NONE])
        button = sender == fNextFilterItem ? fActiveFilterButton : fPauseFilterButton;
    else if ([filterType isEqualToString: FILTER_ACTIVE])
        button = sender == fNextFilterItem ? fDownloadFilterButton : fNoFilterButton;
    else if ([filterType isEqualToString: FILTER_DOWNLOAD])
        button = sender == fNextFilterItem ? fSeedFilterButton : fActiveFilterButton;
    else if ([filterType isEqualToString: FILTER_SEED])
        button = sender == fNextFilterItem ? fPauseFilterButton : fDownloadFilterButton;
    else if ([filterType isEqualToString: FILTER_PAUSE])
        button = sender == fNextFilterItem ? fNoFilterButton : fSeedFilterButton;
    else
        button = fNoFilterButton;
    
    [self setFilter: button];
}

- (void) setStatusLabel: (id) sender
{
    NSString * statusLabel;
    switch ([sender tag])
    {
        case STATUS_RATIO_TOTAL_TAG:
            statusLabel = STATUS_RATIO_TOTAL;
            break;
        case STATUS_RATIO_SESSION_TAG:
            statusLabel = STATUS_RATIO_SESSION;
            break;
        case STATUS_TRANSFER_TOTAL_TAG:
            statusLabel = STATUS_TRANSFER_TOTAL;
            break;
        case STATUS_TRANSFER_SESSION_TAG:
            statusLabel = STATUS_TRANSFER_SESSION;
            break;
        default:
            return;
    }
    
    [fDefaults setObject: statusLabel forKey: @"StatusLabel"];
    [self updateUI];
}

- (void) menuNeedsUpdate: (NSMenu *) menu
{
    if (menu == fGroupsSetMenu || menu == fGroupsSetContextMenu || menu == fGroupFilterMenu)
    {
        const BOOL filter = menu == fGroupFilterMenu;
        
        const NSInteger remaining = filter ? 3 : 0;
        for (NSInteger i = [menu numberOfItems]-1; i >= remaining; i--)
            [menu removeItemAtIndex: i];
        
        NSMenu * groupMenu;
        if (!filter)
            groupMenu = [[GroupsController groups] groupMenuWithTarget: self action: @selector(setGroup:) isSmall: NO];
        else
            groupMenu = [[GroupsController groups] groupMenuWithTarget: self action: @selector(setGroupFilter:) isSmall: YES];
        
        const NSInteger groupMenuCount = [groupMenu numberOfItems];
        for (NSInteger i = 0; i < groupMenuCount; i++)
        {
            NSMenuItem * item = [[groupMenu itemAtIndex: 0] retain];
            [groupMenu removeItemAtIndex: 0];
            [menu addItem: item];
            [item release];
        }
    }
    else if (menu == fUploadMenu || menu == fDownloadMenu)
    {
        if ([menu numberOfItems] > 3)
            return;
        
        const NSInteger speedLimitActionValue[] = { 5, 10, 20, 30, 40, 50, 75, 100, 150, 200, 250, 500, 750, -1 };
        
        NSMenuItem * item;
        for (NSInteger i = 0; speedLimitActionValue[i] != -1; i++)
        {
            item = [[NSMenuItem alloc] initWithTitle: [NSString stringWithFormat: NSLocalizedString(@"%d KB/s",
                    "Action menu -> upload/download limit"), speedLimitActionValue[i]] action: @selector(setQuickLimitGlobal:)
                    keyEquivalent: @""];
            [item setTarget: self];
            [item setRepresentedObject: [NSNumber numberWithInt: speedLimitActionValue[i]]];
            [menu addItem: item];
            [item release];
        }
    }
    else if (menu == fRatioStopMenu)
    {
        if ([menu numberOfItems] > 3)
            return;
        
        const float ratioLimitActionValue[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, -1 };
        
        NSMenuItem * item;
        for (NSInteger i = 0; ratioLimitActionValue[i] != -1; i++)
        {
            item = [[NSMenuItem alloc] initWithTitle: [NSString localizedStringWithFormat: @"%.2f", ratioLimitActionValue[i]]
                    action: @selector(setQuickRatioGlobal:) keyEquivalent: @""];
            [item setTarget: self];
            [item setRepresentedObject: [NSNumber numberWithFloat: ratioLimitActionValue[i]]];
            [menu addItem: item];
            [item release];
        }
    }
    else;
}

- (void) setGroup: (id) sender
{
    for (Torrent * torrent in [fTableView selectedTorrents])
    {
        [fTableView removeCollapsedGroup: [torrent groupValue]]; //remove old collapsed group
        
        [torrent setGroupValue: [sender tag]];
    }
    
    [self applyFilter: nil];
    [self updateUI];
    [self updateTorrentHistory];
}

- (void) setGroupFilter: (id) sender
{
    [fDefaults setInteger: [sender tag] forKey: @"FilterGroup"];
    [self updateGroupsFilterButton];
    [self applyFilter: nil];
}

- (void) updateGroupsFilterButton
{
    NSInteger groupIndex = [fDefaults integerForKey: @"FilterGroup"];
    
    NSImage * icon;
    NSString * toolTip;
    if (groupIndex == GROUP_FILTER_ALL_TAG)
    {
        icon = [NSImage imageNamed: @"PinTemplate.png"];
        toolTip = NSLocalizedString(@"All Groups", "Groups -> Button");
    }
    else
    {
        icon = [[GroupsController groups] imageForIndex: groupIndex];
        NSString * groupName = groupIndex != -1 ? [[GroupsController groups] nameForIndex: groupIndex]
                                                : NSLocalizedString(@"None", "Groups -> Button");
        toolTip = [NSLocalizedString(@"Group", "Groups -> Button") stringByAppendingFormat: @": %@", groupName];
    }
    
    [[fGroupFilterMenu itemAtIndex: 0] setImage: icon];
    [fGroupsButton setToolTip: toolTip];
}

- (void) updateGroupsFilters: (NSNotification *) notification
{
    [self updateGroupsFilterButton];
    [self applyFilter: nil];
}

- (void) toggleSpeedLimit: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"SpeedLimit"] forKey: @"SpeedLimit"];
    [self speedLimitChanged: sender];
}

- (void) speedLimitChanged: (id) sender
{
    [fPrefsController applySpeedSettings: nil];
}

- (void) autoSpeedLimitChange: (NSNotification *) notification
{
    //clear timer here in case it's not being reset
    [fSpeedLimitTimer invalidate];
    fSpeedLimitTimer = nil;
    
    if (![fDefaults boolForKey: @"SpeedLimitAuto"])
        return;
    
    NSCalendar * calendar = [NSCalendar currentCalendar];
    NSDateComponents * nowComponents = [calendar components: NSHourCalendarUnit | NSMinuteCalendarUnit fromDate: [NSDate date]],
                    * onComponents = [calendar components: NSHourCalendarUnit | NSMinuteCalendarUnit
                                        fromDate: [fDefaults objectForKey: @"SpeedLimitAutoOnDate"]],
                    * offComponents = [calendar components: NSHourCalendarUnit | NSMinuteCalendarUnit
                                        fromDate: [fDefaults objectForKey: @"SpeedLimitAutoOffDate"]];
    
    //check if should be on if within range
    NSInteger onTime = [onComponents hour] * 60 + [onComponents minute],
        offTime = [offComponents hour] * 60 + [offComponents minute],
        nowTime = [nowComponents hour] * 60 + [nowComponents minute];
    
    BOOL shouldBeOn = NO;
    if (onTime < offTime)
        shouldBeOn = onTime <= nowTime && nowTime < offTime;
    else if (onTime > offTime)
        shouldBeOn = onTime <= nowTime || nowTime < offTime;
    else;
    
    if ([fDefaults boolForKey: @"SpeedLimit"] != shouldBeOn)
        [self toggleSpeedLimit: nil];
    
    //no need to set the timer if both times are equal
    if (onTime == offTime)
        return;
    
    [self setAutoSpeedLimitTimer: !shouldBeOn];
}

//only called by fSpeedLimitTimer
- (void) autoSpeedLimit: (NSTimer *) timer
{
    BOOL shouldLimit = [[timer userInfo] boolValue];
    
    if ([fDefaults boolForKey: @"SpeedLimit"] != shouldLimit)
    {
        [self toggleSpeedLimit: nil];
        
        [GrowlApplicationBridge notifyWithTitle: [fDefaults boolForKey: @"SpeedLimit"]
                ? NSLocalizedString(@"Speed Limit Auto Enabled", "Growl notification title")
                : NSLocalizedString(@"Speed Limit Auto Disabled", "Growl notification title")
            description: NSLocalizedString(@"Bandwidth settings changed", "Growl notification description")
            notificationName: GROWL_AUTO_SPEED_LIMIT iconData: nil priority: 0 isSticky: NO clickContext: nil];
    }
    
    [self setAutoSpeedLimitTimer: !shouldLimit];
}

- (void) setAutoSpeedLimitTimer: (BOOL) nextIsLimit
{
    NSDate * timerDate = [fDefaults objectForKey: nextIsLimit ? @"SpeedLimitAutoOnDate" : @"SpeedLimitAutoOffDate"];
    
    //create date with combination of the current date and the date to go off
    NSCalendar * calendar = [NSCalendar currentCalendar];
    NSDateComponents * nowComponents = [calendar components: NSYearCalendarUnit | NSMonthCalendarUnit | NSDayCalendarUnit
                                        | NSHourCalendarUnit | NSMinuteCalendarUnit fromDate: [NSDate date]],
                    * timerComponents = [calendar components: NSHourCalendarUnit | NSMinuteCalendarUnit fromDate: timerDate];
    
    //check if should be the next day
    NSInteger nowTime = [nowComponents hour] * 60 + [nowComponents minute],
        timerTime = [timerComponents hour] * 60 + [timerComponents minute];
    if (timerTime < nowTime)
        [nowComponents setDay: [nowComponents day] + 1]; //properly goes to next month when appropriate
    
    [nowComponents setHour: [timerComponents hour]];
    [nowComponents setMinute: [timerComponents minute]];
    [nowComponents setSecond: 0];
    
    NSDate * dateToUse = [calendar dateFromComponents: nowComponents];
    
    fSpeedLimitTimer = [[NSTimer alloc] initWithFireDate: dateToUse interval: 0 target: self selector: @selector(autoSpeedLimit:)
                        userInfo: [NSNumber numberWithBool: nextIsLimit] repeats: NO];
    
    NSRunLoop * loop = [NSRunLoop mainRunLoop];
    [loop addTimer: fSpeedLimitTimer forMode: NSDefaultRunLoopMode];
    [loop addTimer: fSpeedLimitTimer forMode: NSModalPanelRunLoopMode];
    [loop addTimer: fSpeedLimitTimer forMode: NSEventTrackingRunLoopMode];
    [fSpeedLimitTimer release];
}

- (void) setLimitGlobalEnabled: (id) sender
{
    BOOL upload = [sender menu] == fUploadMenu;
    [fDefaults setBool: sender == (upload ? fUploadLimitItem : fDownloadLimitItem) forKey: upload ? @"CheckUpload" : @"CheckDownload"];
    
    [fPrefsController applySpeedSettings: nil];
}

- (void) setQuickLimitGlobal: (id) sender
{
    BOOL upload = [sender menu] == fUploadMenu;
    [fDefaults setInteger: [[sender representedObject] intValue] forKey: upload ? @"UploadLimit" : @"DownloadLimit"];
    [fDefaults setBool: YES forKey: upload ? @"CheckUpload" : @"CheckDownload"];
    
    [fPrefsController updateLimitFields];
    [fPrefsController applySpeedSettings: nil];
}

- (void) setRatioGlobalEnabled: (id) sender
{
    [fDefaults setBool: sender == fCheckRatioItem forKey: @"RatioCheck"];
    
    [fPrefsController applyRatioSetting: nil];
}

- (void) setQuickRatioGlobal: (id) sender
{
    [fDefaults setBool: YES forKey: @"RatioCheck"];
    [fDefaults setFloat: [[sender representedObject] floatValue] forKey: @"RatioLimit"];
    
    [fPrefsController updateRatioStopField];
}

- (void) sound: (NSSound *) sound didFinishPlaying: (BOOL) finishedPlaying
{
    fSoundPlaying = NO;
}

- (void) watcher: (id<UKFileWatcher>) watcher receivedNotification: (NSString *) notification forPath: (NSString *) path
{
    if ([notification isEqualToString: UKFileWatcherWriteNotification])
    {
        if (![fDefaults boolForKey: @"AutoImport"] || ![fDefaults stringForKey: @"AutoImportDirectory"])
            return;
        
        if (fAutoImportTimer)
        {
            if ([fAutoImportTimer isValid])
                [fAutoImportTimer invalidate];
            [fAutoImportTimer release];
            fAutoImportTimer = nil;
        }
        
        //check again in 10 seconds in case torrent file wasn't complete
        fAutoImportTimer = [[NSTimer scheduledTimerWithTimeInterval: 10.0 target: self 
            selector: @selector(checkAutoImportDirectory) userInfo: nil repeats: NO] retain];
        
        [self checkAutoImportDirectory];
    }
}

- (void) changeAutoImport
{
    if (fAutoImportTimer)
    {
        if ([fAutoImportTimer isValid])
            [fAutoImportTimer invalidate];
        [fAutoImportTimer release];
        fAutoImportTimer = nil;
    }
    
    if (fAutoImportedNames)
    {
        [fAutoImportedNames release];
        fAutoImportedNames = nil;
    }
    
    [self checkAutoImportDirectory];
}

- (void) checkAutoImportDirectory
{
    NSString * path;
    if (![fDefaults boolForKey: @"AutoImport"] || !(path = [fDefaults stringForKey: @"AutoImportDirectory"]))
        return;
    
    path = [path stringByExpandingTildeInPath];
    
    NSArray * importedNames;
    if (!(importedNames = [[NSFileManager defaultManager] directoryContentsAtPath: path]))
        return;
    
    //only check files that have not been checked yet
    NSMutableArray * newNames = [importedNames mutableCopy];
    
    if (fAutoImportedNames)
        [newNames removeObjectsInArray: fAutoImportedNames];
    else
        fAutoImportedNames = [[NSMutableArray alloc] init];
    [fAutoImportedNames setArray: importedNames];
    
    for (NSInteger i = [newNames count] - 1; i >= 0; i--)
    {
        NSString * file = [newNames objectAtIndex: i];
        if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] != NSOrderedSame)
            [newNames removeObjectAtIndex: i];
        else
            [newNames replaceObjectAtIndex: i withObject: [path stringByAppendingPathComponent: file]];
    }
    
    for (NSString * file in newNames)
    {
        tr_ctor * ctor = tr_ctorNew(fLib);
        tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
        
        switch (tr_torrentParse(fLib, ctor, NULL))
        {
            case TR_OK:
                [self openFiles: [NSArray arrayWithObject: file] addType: ADD_AUTO forcePath: nil];
                
                [GrowlApplicationBridge notifyWithTitle: NSLocalizedString(@"Torrent File Auto Added", "Growl notification title")
                    description: [file lastPathComponent] notificationName: GROWL_AUTO_ADD iconData: nil priority: 0 isSticky: NO
                    clickContext: nil];
                break;
            
            case TR_EINVALID:
                [fAutoImportedNames removeObject: [file lastPathComponent]];
        }
        
        tr_ctorFree(ctor);
    }
    
    [newNames release];
}

- (void) beginCreateFile: (NSNotification *) notification
{
    if (![fDefaults boolForKey: @"AutoImport"])
        return;
    
    NSString * location = [notification object],
            * path = [fDefaults stringForKey: @"AutoImportDirectory"];
    
    if (location && path && [[[location stringByDeletingLastPathComponent] stringByExpandingTildeInPath]
                                    isEqualToString: [path stringByExpandingTildeInPath]])
        [fAutoImportedNames addObject: [location lastPathComponent]];
}

- (NSInteger) outlineView: (NSOutlineView *) outlineView numberOfChildrenOfItem: (id) item
{
    if (item)
        return [[item torrents] count];
    else
        return [fDisplayedTorrents count];
}

- (id) outlineView: (NSOutlineView *) outlineView child: (NSInteger) index ofItem: (id) item
{
    if (item)
        return [[item torrents] objectAtIndex: index];
    else
        return [fDisplayedTorrents objectAtIndex: index];
}

- (BOOL) outlineView: (NSOutlineView *) outlineView isItemExpandable: (id) item
{
    return ![item isKindOfClass: [Torrent class]];
}

- (id) outlineView: (NSOutlineView *) outlineView objectValueForTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    if ([item isKindOfClass: [Torrent class]])
        return [item hashString];
    else
    {
        NSString * ident = [tableColumn identifier];
        if ([ident isEqualToString: @"Group"])
        {
            NSInteger group = [item groupIndex];
            return group != -1 ? [[GroupsController groups] nameForIndex: group]
                                : NSLocalizedString(@"No Group", "Group table row");
        }
        else if ([ident isEqualToString: @"Color"])
        {
            NSInteger group = [item groupIndex];
            return [[GroupsController groups] imageForIndex: group];
        }
        else if ([ident isEqualToString: @"DL Image"])
            return [NSImage imageNamed: @"DownArrowGroupTemplate.png"];
        else if ([ident isEqualToString: @"UL Image"])
            return [NSImage imageNamed: [fDefaults boolForKey: @"DisplayGroupRowRatio"]
                                        ? @"YingYangGroupTemplate.png" : @"UpArrowGroupTemplate.png"];
        else
        {
            TorrentGroup * group = (TorrentGroup *)item;
            
            if ([fDefaults boolForKey: @"DisplayGroupRowRatio"])
                return [NSString stringForRatio: [group ratio]];
            else
            {
                CGFloat rate = [ident isEqualToString: @"UL"] ? [group uploadRate] : [group downloadRate];
                return [NSString stringForSpeed: rate];
            }
        }
    }
}

- (BOOL) outlineView: (NSOutlineView *) outlineView writeItems: (NSArray *) items toPasteboard: (NSPasteboard *) pasteboard
{
    //only allow reordering of rows if sorting by order
    if ([fDefaults boolForKey: @"SortByGroup"] || [[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER])
    {
        NSMutableIndexSet * indexSet = [NSMutableIndexSet indexSet];
        for (id torrent in items)
        {
            if (![torrent isKindOfClass: [Torrent class]])
                return NO;
            
            [indexSet addIndex: [fTableView rowForItem: torrent]];
        }
        
        [pasteboard declareTypes: [NSArray arrayWithObject: TORRENT_TABLE_VIEW_DATA_TYPE] owner: self];
        [pasteboard setData: [NSKeyedArchiver archivedDataWithRootObject: indexSet] forType: TORRENT_TABLE_VIEW_DATA_TYPE];
        return YES;
    }
    return NO;
}

- (NSDragOperation) outlineView: (NSOutlineView *) outlineView validateDrop: (id < NSDraggingInfo >) info proposedItem: (id) item
    proposedChildIndex: (NSInteger) index
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        if ([fDefaults boolForKey: @"SortByGroup"])
        {
            if (!item)
                return NSDragOperationNone;
            
            if ([[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER])
            {
                if ([item isKindOfClass: [Torrent class]])
                {
                    TorrentGroup * group = [fTableView parentForItem: item];
                    index = [[group torrents] indexOfObject: item] + 1;
                    item = group;
                }
            }
            else
            {
                if ([item isKindOfClass: [Torrent class]])
                    item = [fTableView parentForItem: item];
                index = NSOutlineViewDropOnItemIndex;
            }
        }
        else
        {
            if (item)
            {
                index = [fTableView rowForItem: item] + 1;
                item = nil;
            }
        }
        
        [fTableView setDropItem: item dropChildIndex: index];
        return NSDragOperationGeneric;
    }
    
    return NSDragOperationNone;
}

- (BOOL) outlineView: (NSOutlineView *) outlineView acceptDrop: (id < NSDraggingInfo >) info item: (id) item
    childIndex: (NSInteger) newRow
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        //remember selected rows
        NSArray * selectedValues = [fTableView selectedValues];
    
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData: [pasteboard dataForType: TORRENT_TABLE_VIEW_DATA_TYPE]];
        
        //get the torrents to move
        NSMutableArray * movingTorrents = [NSMutableArray arrayWithCapacity: [indexes count]];
        for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
            [movingTorrents addObject: [fTableView itemAtRow: i]];
        
        //reset groups
        if (item)
        {
            //change groups
            NSInteger groupValue = [item groupIndex];
            for (Torrent * torrent in movingTorrents)
            {
                //have to reset objects here to avoid weird crash
                [[[fTableView parentForItem: torrent] torrents] removeObject: torrent];
                [[item torrents] addObject: torrent];
                
                [torrent setGroupValue: groupValue];
            }
            //part 2 of avoiding weird crash
            [fTableView reloadItem: nil reloadChildren: YES];
        }
        
        //reorder queue order
        if (newRow != NSOutlineViewDropOnItemIndex)
        {
            //find torrent to place under
            NSArray * groupTorrents = item ? [item torrents] : fDisplayedTorrents;
            Torrent * topTorrent = nil;
            for (NSInteger i = newRow-1; i >= 0; i--)
            {
                Torrent * tempTorrent = [groupTorrents objectAtIndex: i];
                if (![movingTorrents containsObject: tempTorrent])
                {
                    topTorrent = tempTorrent;
                    break;
                }
            }
            
            //remove objects to reinsert
            [fTorrents removeObjectsInArray: movingTorrents];
            
            //insert objects at new location
            NSUInteger insertIndex = topTorrent ? [fTorrents indexOfObject: topTorrent] + 1 : 0;
            NSIndexSet * insertIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(insertIndex, [movingTorrents count])];
            [fTorrents insertObjects: movingTorrents atIndexes: insertIndexes];
        }
        
        [self applyFilter: nil];
        [fTableView selectValues: selectedValues];
    }
    
    return YES;
}

- (void) torrentTableViewSelectionDidChange: (NSNotification *) notification
{
    [self resetInfo];
}

- (NSDragOperation) draggingEntered: (id <NSDraggingInfo>) info
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        //check if any torrent files can be added
        BOOL torrent = NO;
        NSArray * files = [pasteboard propertyListForType: NSFilenamesPboardType];
        for (NSString * file in files)
        {
            if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
            {
                tr_ctor * ctor = tr_ctorNew(fLib);
                tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
                switch (tr_torrentParse(fLib, ctor, NULL))
                {
                    case TR_OK:
                        if (!fOverlayWindow)
                            fOverlayWindow = [[DragOverlayWindow alloc] initWithLib: fLib forWindow: fWindow];
                        [fOverlayWindow setTorrents: files];
                        
                        return NSDragOperationCopy;
                    
                    case TR_EDUPLICATE:
                        torrent = YES;
                }
                tr_ctorFree(ctor);
            }
        }
        
        //create a torrent file if a single file
        if (!torrent && [files count] == 1)
        {
            if (!fOverlayWindow)
                fOverlayWindow = [[DragOverlayWindow alloc] initWithLib: fLib forWindow: fWindow];
            [fOverlayWindow setFile: [[files objectAtIndex: 0] lastPathComponent]];
            
            return NSDragOperationCopy;
        }
    }
    else if ([[pasteboard types] containsObject: NSURLPboardType])
    {
        if (!fOverlayWindow)
            fOverlayWindow = [[DragOverlayWindow alloc] initWithLib: fLib forWindow: fWindow];
        [fOverlayWindow setURL: [[NSURL URLFromPasteboard: pasteboard] relativeString]];
        
        return NSDragOperationCopy;
    }
    else;
    
    return NSDragOperationNone;
}

- (void) draggingExited: (id <NSDraggingInfo>) info
{
    if (fOverlayWindow)
        [fOverlayWindow fadeOut];
}

- (BOOL) performDragOperation: (id <NSDraggingInfo>) info
{
    if (fOverlayWindow)
        [fOverlayWindow fadeOut];
    
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        BOOL torrent = NO, accept = YES;
        
        //create an array of files that can be opened
        NSMutableArray * filesToOpen = [[NSMutableArray alloc] init];
        NSArray * files = [pasteboard propertyListForType: NSFilenamesPboardType];
        for (NSString * file in files)
        {
            if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
            {
                tr_ctor * ctor = tr_ctorNew(fLib);
                tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
                switch (tr_torrentParse(fLib, ctor, NULL))
                {
                    case TR_OK:
                        [filesToOpen addObject: file];
                        torrent = YES;
                        break;
                        
                    case TR_EDUPLICATE:
                        torrent = YES;
                }
                tr_ctorFree(ctor);
            }
        }
        
        if ([filesToOpen count] > 0)
            [self application: NSApp openFiles: filesToOpen];
        else
        {
            if (!torrent && [files count] == 1)
                [CreatorWindowController createTorrentFile: fLib forFile: [files objectAtIndex: 0]];
            else
                accept = NO;
        }
        [filesToOpen release];
        
        return accept;
    }
    else if ([[pasteboard types] containsObject: NSURLPboardType])
    {
        NSURL * url;
        if ((url = [NSURL URLFromPasteboard: pasteboard]))
        {
            [self openURL: url];
            return YES;
        }
    }
    else;
    
    return NO;
}

- (void) toggleSmallView: (id) sender
{
    BOOL makeSmall = ![fDefaults boolForKey: @"SmallView"];
    [fDefaults setBool: makeSmall forKey: @"SmallView"];
    
    [fTableView setRowHeight: makeSmall ? ROW_HEIGHT_SMALL : ROW_HEIGHT_REGULAR];
    
    [fTableView noteHeightOfRowsWithIndexesChanged: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTableView numberOfRows])]];
    
    //window min height
    NSSize contentMinSize = [fWindow contentMinSize],
            contentSize = [[fWindow contentView] frame].size;
    contentMinSize.height = contentSize.height - [[fTableView enclosingScrollView] frame].size.height
                            + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    [fWindow setContentMinSize: contentMinSize];
    
    //resize for larger min height if not set to auto size
    if (![fDefaults boolForKey: @"AutoSize"])
    {
        if (!makeSmall && contentSize.height < contentMinSize.height)
        {
            NSRect frame = [fWindow frame];
            CGFloat heightChange = contentMinSize.height - contentSize.height;
            frame.size.height += heightChange;
            frame.origin.y -= heightChange;
            
            [fWindow setFrame: frame display: YES];
        }
    }
    else
        [self setWindowSizeToFit];
}

- (void) togglePiecesBar: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"PiecesBar"] forKey: @"PiecesBar"];
    [fTableView togglePiecesBar];
}

- (void) toggleAvailabilityBar: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"DisplayProgressBarAvailable"] forKey: @"DisplayProgressBarAvailable"];
    [fTableView display];
}

- (void) toggleStatusString: (id) sender
{
    if ([fDefaults boolForKey: @"SmallView"])
        [fDefaults setBool: ![fDefaults boolForKey: @"DisplaySmallStatusRegular"] forKey: @"DisplaySmallStatusRegular"];
    else
        [fDefaults setBool: ![fDefaults boolForKey: @"DisplayStatusProgressSelected"] forKey: @"DisplayStatusProgressSelected"];
    
    [fTableView reloadData];
}

- (NSRect) windowFrameByAddingHeight: (CGFloat) height checkLimits: (BOOL) check
{
    NSScrollView * scrollView = [fTableView enclosingScrollView];
    
    //convert pixels to points
    NSRect windowFrame = [fWindow frame];
    NSSize windowSize = [scrollView convertSize: windowFrame.size fromView: nil];
    windowSize.height += height;
    
    if (check)
    {
        NSSize minSize = [scrollView convertSize: [fWindow minSize] fromView: nil];
        
        if (windowSize.height < minSize.height)
            windowSize.height = minSize.height;
        else
        {
            NSSize maxSize = [scrollView convertSize: [[fWindow screen] visibleFrame].size fromView: nil];
            if ([fStatusBar isHidden])
                maxSize.height -= [fStatusBar frame].size.height;
            if ([fFilterBar isHidden]) 
                maxSize.height -= [fFilterBar frame].size.height;
            if (windowSize.height > maxSize.height)
                windowSize.height = maxSize.height;
        }
    }

    //convert points to pixels
    windowSize = [scrollView convertSize: windowSize toView: nil];

    windowFrame.origin.y -= (windowSize.height - windowFrame.size.height);
    windowFrame.size.height = windowSize.height;
    return windowFrame;
}

- (void) toggleStatusBar: (id) sender
{
    [self showStatusBar: [fStatusBar isHidden] animate: YES];
    [fDefaults setBool: ![fStatusBar isHidden] forKey: @"StatusBar"];
}

//doesn't save shown state
- (void) showStatusBar: (BOOL) show animate: (BOOL) animate
{
    if (show != [fStatusBar isHidden])
        return;

    if (show)
        [fStatusBar setHidden: NO];

    NSRect frame;
    CGFloat heightChange = [fStatusBar frame].size.height;
    if (!show)
        heightChange *= -1;
    
    //allow bar to show even if not enough room
    if (show && ![fDefaults boolForKey: @"AutoSize"])
    {
        frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
        CGFloat change = [[fWindow screen] visibleFrame].size.height - frame.size.height;
        if (change < 0.0)
        {
            frame = [fWindow frame];
            frame.size.height += change;
            frame.origin.y -= change;
            [fWindow setFrame: frame display: NO animate: NO];
        }
    }

    [self updateUI];
    
    NSScrollView * scrollView = [fTableView enclosingScrollView];
    
    //set views to not autoresize
    NSUInteger statsMask = [fStatusBar autoresizingMask];
    NSUInteger filterMask = [fFilterBar autoresizingMask];
    NSUInteger scrollMask = [scrollView autoresizingMask];
    [fStatusBar setAutoresizingMask: NSViewNotSizable];
    [fFilterBar setAutoresizingMask: NSViewNotSizable];
    [scrollView setAutoresizingMask: NSViewNotSizable];
    
    frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
    [fWindow setFrame: frame display: YES animate: animate]; 
    
    //re-enable autoresize
    [fStatusBar setAutoresizingMask: statsMask];
    [fFilterBar setAutoresizingMask: filterMask];
    [scrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
    
    if (!show)
        [fStatusBar setHidden: YES];
}

- (void) toggleFilterBar: (id) sender
{
    //disable filtering when hiding
    if (![fFilterBar isHidden])
    {
        [fSearchFilterField setStringValue: @""];
        [self setFilter: fNoFilterButton];
        [self setGroupFilter: [fGroupFilterMenu itemWithTag: GROUP_FILTER_ALL_TAG]];
    }

    [self showFilterBar: [fFilterBar isHidden] animate: YES];
    [fDefaults setBool: ![fFilterBar isHidden] forKey: @"FilterBar"];
}

//doesn't save shown state
- (void) showFilterBar: (BOOL) show animate: (BOOL) animate
{
    if (show != [fFilterBar isHidden])
        return;

    if (show)
        [fFilterBar setHidden: NO];

    NSRect frame;
    CGFloat heightChange = [fFilterBar frame].size.height;
    if (!show)
        heightChange *= -1;
    
    //allow bar to show even if not enough room
    if (show && ![fDefaults boolForKey: @"AutoSize"])
    {
        frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
        CGFloat change = [[fWindow screen] visibleFrame].size.height - frame.size.height;
        if (change < 0.0)
        {
            frame = [fWindow frame];
            frame.size.height += change;
            frame.origin.y -= change;
            [fWindow setFrame: frame display: NO animate: NO];
        }
    }
    
    NSScrollView * scrollView = [fTableView enclosingScrollView];

    //set views to not autoresize
    NSUInteger filterMask = [fFilterBar autoresizingMask];
    NSUInteger scrollMask = [scrollView autoresizingMask];
    [fFilterBar setAutoresizingMask: NSViewNotSizable];
    [scrollView setAutoresizingMask: NSViewNotSizable];
    
    frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
    [fWindow setFrame: frame display: YES animate: animate];
    
    //re-enable autoresize
    [fFilterBar setAutoresizingMask: filterMask];
    [scrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
    
    if (!show)
    {
        [fFilterBar setHidden: YES];
        [fWindow makeFirstResponder: fTableView];
    }
}

- (void) focusFilterField
{
    [fWindow makeFirstResponder: fSearchFilterField];
    if ([fFilterBar isHidden])
        [self toggleFilterBar: self];
}

- (ButtonToolbarItem *) standardToolbarButtonWithIdentifier: (NSString *) ident
{
    ButtonToolbarItem * item = [[ButtonToolbarItem alloc] initWithItemIdentifier: ident];
    
    NSButton * button = [[NSButton alloc] initWithFrame: NSZeroRect];
    [button setBezelStyle: NSTexturedRoundedBezelStyle];
    [button setStringValue: @""];
    
    [item setView: button];
    [button release];
    
    const NSSize buttonSize = NSMakeSize(36.0, 25.0);
    [item setMinSize: buttonSize];
    [item setMaxSize: buttonSize];
    
    return [item autorelease];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) toolbar itemForItemIdentifier: (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    if ([ident isEqualToString: TOOLBAR_CREATE])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Create", "Create toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Create Torrent File", "Create toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Create torrent file", "Create toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarCreateTemplate.png"]];
        [item setTarget: self];
        [item setAction: @selector(createFile:)];
        [item setAutovalidates: NO];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_OPEN_FILE])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Open", "Open toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Open Torrent Files", "Open toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Open torrent files", "Open toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarOpenTemplate.png"]];
        [item setTarget: self];
        [item setAction: @selector(openShowSheet:)];
        [item setAutovalidates: NO];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_OPEN_WEB])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Open Address", "Open address toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Open Torrent Address", "Open address toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Open torrent web address", "Open address toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarOpenWebTemplate.png"]];
        [item setTarget: self];
        [item setAction: @selector(openURLShowSheet:)];
        [item setAutovalidates: NO];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_REMOVE])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Remove", "Remove toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Remove Selected", "Remove toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Remove selected transfers", "Remove toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarRemoveTemplate.png"]];
        [item setTarget: self];
        [item setAction: @selector(removeNoDelete:)];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_INFO])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        [[(NSButton *)[item view] cell] setShowsStateBy: NSContentsCellMask]; //blue when enabled
        
        [item setLabel: NSLocalizedString(@"Inspector", "Inspector toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Toggle Inspector", "Inspector toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Toggle the torrent inspector", "Inspector toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarInfoTemplate.png"]];
        [item setTarget: self];
        [item setAction: @selector(showInfo:)];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_RESUME_ALL])
    {
        GroupToolbarItem * groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier: ident];
        
        NSSegmentedControl * segmentedControl = [[NSSegmentedControl alloc] initWithFrame: NSZeroRect];
        [segmentedControl setCell: [[[ToolbarSegmentedCell alloc] init] autorelease]];
        [groupItem setView: segmentedControl];
        NSSegmentedCell * segmentedCell = (NSSegmentedCell *)[segmentedControl cell];
        
        [segmentedControl setSegmentCount: 2];
        [segmentedCell setTrackingMode: NSSegmentSwitchTrackingMomentary];
        
        const NSSize groupSize = NSMakeSize(72.0, 25.0);
        [groupItem setMinSize: groupSize];
        [groupItem setMaxSize: groupSize];
        
        [groupItem setLabel: NSLocalizedString(@"Apply All", "All toolbar item -> label")];
        [groupItem setPaletteLabel: NSLocalizedString(@"Pause / Resume All", "All toolbar item -> palette label")];
        [groupItem setTarget: self];
        [groupItem setAction: @selector(allToolbarClicked:)];
        
        [groupItem setIdentifiers: [NSArray arrayWithObjects: TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL, nil]];
        
        [segmentedCell setTag: TOOLBAR_PAUSE_TAG forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ToolbarPauseAllTemplate.png"] forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Pause all transfers",
                                    "All toolbar item -> tooltip") forSegment: TOOLBAR_PAUSE_TAG];
        
        [segmentedCell setTag: TOOLBAR_RESUME_TAG forSegment: TOOLBAR_RESUME_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ToolbarResumeAllTemplate.png"] forSegment: TOOLBAR_RESUME_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Resume all transfers",
                                    "All toolbar item -> tooltip") forSegment: TOOLBAR_RESUME_TAG];
        
        [groupItem createMenu: [NSArray arrayWithObjects: NSLocalizedString(@"Pause All", "All toolbar item -> label"),
                                        NSLocalizedString(@"Resume All", "All toolbar item -> label"), nil]];
        
        [segmentedControl release];
        return [groupItem autorelease];
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_RESUME_SELECTED])
    {
        GroupToolbarItem * groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier: ident];
        
        NSSegmentedControl * segmentedControl = [[NSSegmentedControl alloc] initWithFrame: NSZeroRect];
        [segmentedControl setCell: [[[ToolbarSegmentedCell alloc] init] autorelease]];
        [groupItem setView: segmentedControl];
        NSSegmentedCell * segmentedCell = (NSSegmentedCell *)[segmentedControl cell];
        
        [segmentedControl setSegmentCount: 2];
        [segmentedCell setTrackingMode: NSSegmentSwitchTrackingMomentary];
        
        const NSSize groupSize = NSMakeSize(72.0, 25.0);
        [groupItem setMinSize: groupSize];
        [groupItem setMaxSize: groupSize];
        
        [groupItem setLabel: NSLocalizedString(@"Apply Selected", "Selected toolbar item -> label")];
        [groupItem setPaletteLabel: NSLocalizedString(@"Pause / Resume Selected", "Selected toolbar item -> palette label")];
        [groupItem setTarget: self];
        [groupItem setAction: @selector(selectedToolbarClicked:)];
        
        [groupItem setIdentifiers: [NSArray arrayWithObjects: TOOLBAR_PAUSE_SELECTED, TOOLBAR_RESUME_SELECTED, nil]];
        
        [segmentedCell setTag: TOOLBAR_PAUSE_TAG forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ToolbarPauseSelectedTemplate.png"] forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Pause selected transfers",
                                    "Selected toolbar item -> tooltip") forSegment: TOOLBAR_PAUSE_TAG];
        
        [segmentedCell setTag: TOOLBAR_RESUME_TAG forSegment: TOOLBAR_RESUME_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ToolbarResumeSelectedTemplate.png"] forSegment: TOOLBAR_RESUME_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Resume selected transfers",
                                    "Selected toolbar item -> tooltip") forSegment: TOOLBAR_RESUME_TAG];
        
        [groupItem createMenu: [NSArray arrayWithObjects: NSLocalizedString(@"Pause Selected", "Selected toolbar item -> label"),
                                        NSLocalizedString(@"Resume Selected", "Selected toolbar item -> label"), nil]];
        
        [segmentedControl release];
        return [groupItem autorelease];
    }
    else if ([ident isEqualToString: TOOLBAR_FILTER])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        [[(NSButton *)[item view] cell] setShowsStateBy: NSContentsCellMask]; //blue when enabled
        
        [item setLabel: NSLocalizedString(@"Filter", "Filter toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Toggle Filter", "Filter toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Toggle the filter bar", "Filter toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarFilterTemplate.png"]];
        [item setTarget: self];
        [item setAction: @selector(toggleFilterBar:)];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_QUICKLOOK])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: NSImageNameQuickLookTemplate]];
        [item setTarget: self];
        [item setAction: @selector(toggleQuickLook:)];
        
        return item;
    }
    else
        return nil;
}

- (void) allToolbarClicked: (id) sender
{
    NSInteger tagValue = [sender isKindOfClass: [NSSegmentedControl class]]
                    ? [(NSSegmentedCell *)[sender cell] tagForSegment: [sender selectedSegment]] : [sender tag];
    switch (tagValue)
    {
        case TOOLBAR_PAUSE_TAG:
            [self stopAllTorrents: sender];
            break;
        case TOOLBAR_RESUME_TAG:
            [self resumeAllTorrents: sender];
            break;
    }
}

- (void) selectedToolbarClicked: (id) sender
{
    NSInteger tagValue = [sender isKindOfClass: [NSSegmentedControl class]]
                    ? [(NSSegmentedCell *)[sender cell] tagForSegment: [sender selectedSegment]] : [sender tag];
    switch (tagValue)
    {
        case TOOLBAR_PAUSE_TAG:
            [self stopSelectedTorrents: sender];
            break;
        case TOOLBAR_RESUME_TAG:
            [self resumeSelectedTorrents: sender];
            break;
    }
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) toolbar
{
    return [NSArray arrayWithObjects:
            TOOLBAR_CREATE, TOOLBAR_OPEN_FILE, TOOLBAR_OPEN_WEB, TOOLBAR_REMOVE,
            TOOLBAR_PAUSE_RESUME_SELECTED, TOOLBAR_PAUSE_RESUME_ALL,
            TOOLBAR_QUICKLOOK, TOOLBAR_FILTER, TOOLBAR_INFO,
            NSToolbarSeparatorItemIdentifier,
            NSToolbarSpaceItemIdentifier,
            NSToolbarFlexibleSpaceItemIdentifier,
            NSToolbarCustomizeToolbarItemIdentifier, nil];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) toolbar
{
    return [NSArray arrayWithObjects:
            TOOLBAR_CREATE, TOOLBAR_OPEN_FILE, TOOLBAR_REMOVE, NSToolbarSeparatorItemIdentifier,
            TOOLBAR_PAUSE_RESUME_ALL, NSToolbarFlexibleSpaceItemIdentifier,
            TOOLBAR_QUICKLOOK, TOOLBAR_FILTER, TOOLBAR_INFO, nil];
}

- (BOOL) validateToolbarItem: (NSToolbarItem *) toolbarItem
{
    NSString * ident = [toolbarItem itemIdentifier];
    
    //enable remove item
    if ([ident isEqualToString: TOOLBAR_REMOVE])
        return [fTableView numberOfSelectedRows] > 0;

    //enable pause all item
    if ([ident isEqualToString: TOOLBAR_PAUSE_ALL])
    {
        for (Torrent * torrent in fTorrents)
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable resume all item
    if ([ident isEqualToString: TOOLBAR_RESUME_ALL])
    {
        for (Torrent * torrent in fTorrents)
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable pause item
    if ([ident isEqualToString: TOOLBAR_PAUSE_SELECTED])
    {
        for (Torrent * torrent in [fTableView selectedTorrents])
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume item
    if ([ident isEqualToString: TOOLBAR_RESUME_SELECTED])
    {
        for (Torrent * torrent in [fTableView selectedTorrents])
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //set info image
    if ([ident isEqualToString: TOOLBAR_INFO])
    {
        [(NSButton *)[toolbarItem view] setState: [[fInfoController window] isVisible]];
        return YES;
    }
    
    //set filter image
    if ([ident isEqualToString: TOOLBAR_FILTER])
    {
        [(NSButton *)[toolbarItem view] setState: ![fFilterBar isHidden]];
        return YES;
    }
    
    //enable quicklook item
    if ([ident isEqualToString: TOOLBAR_QUICKLOOK])
        return [[QuickLookController quickLook] canQuickLook];

    return YES;
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];
    
    if (action == @selector(toggleSpeedLimit:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SpeedLimit"] ? NSOnState : NSOffState];
        return YES;
    }
    
    //only enable some items if it is in a context menu or the window is useable
    BOOL canUseTable = [fWindow isKeyWindow] || [[menuItem menu] supermenu] != [NSApp mainMenu];

    //enable open items
    if (action == @selector(openShowSheet:) || action == @selector(openURLShowSheet:))
        return [fWindow attachedSheet] == nil;
    
    //enable sort options
    if (action == @selector(setSort:))
    {
        NSString * sortType;
        switch ([menuItem tag])
        {
            case SORT_ORDER_TAG:
                sortType = SORT_ORDER;
                break;
            case SORT_DATE_TAG:
                sortType = SORT_DATE;
                break;
            case SORT_NAME_TAG:
                sortType = SORT_NAME;
                break;
            case SORT_PROGRESS_TAG:
                sortType = SORT_PROGRESS;
                break;
            case SORT_STATE_TAG:
                sortType = SORT_STATE;
                break;
            case SORT_TRACKER_TAG:
                sortType = SORT_TRACKER;
                break;
            case SORT_ACTIVITY_TAG:
                sortType = SORT_ACTIVITY;
        }
        
        [menuItem setState: [sortType isEqualToString: [fDefaults stringForKey: @"Sort"]] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }
    
    //enable sort options
    if (action == @selector(setStatusLabel:))
    {
        NSString * statusLabel;
        switch ([menuItem tag])
        {
            case STATUS_RATIO_TOTAL_TAG:
                statusLabel = STATUS_RATIO_TOTAL;
                break;
            case STATUS_RATIO_SESSION_TAG:
                statusLabel = STATUS_RATIO_SESSION;
                break;
            case STATUS_TRANSFER_TOTAL_TAG:
                statusLabel = STATUS_TRANSFER_TOTAL;
                break;
            case STATUS_TRANSFER_SESSION_TAG:
                statusLabel = STATUS_TRANSFER_SESSION;
        }
        
        [menuItem setState: [statusLabel isEqualToString: [fDefaults stringForKey: @"StatusLabel"]] ? NSOnState : NSOffState];
        return YES;
    }
    
    if (action == @selector(setGroup:))
    {
        BOOL checked = NO;
        
        NSInteger index = [menuItem tag];
        for (Torrent * torrent in [fTableView selectedTorrents])
            if (index == [torrent groupValue])
            {
                checked = YES;
                break;
            }
        
        [menuItem setState: checked ? NSOnState : NSOffState];
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    }
    
    if (action == @selector(setGroupFilter:))
    {
        [menuItem setState: [menuItem tag] == [fDefaults integerForKey: @"FilterGroup"] ? NSOnState : NSOffState];
        return YES;
    }
    
    if (action == @selector(toggleSmallView:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SmallView"] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }
    
    if (action == @selector(togglePiecesBar:))
    {
        [menuItem setState: [fDefaults boolForKey: @"PiecesBar"] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }
    
    if (action == @selector(toggleStatusString:))
    {
        if ([fDefaults boolForKey: @"SmallView"])
        {
            [menuItem setTitle: NSLocalizedString(@"Remaining Time", "Action menu -> status string toggle")];
            [menuItem setState: ![fDefaults boolForKey: @"DisplaySmallStatusRegular"] ? NSOnState : NSOffState];
        }
        else
        {
            [menuItem setTitle: NSLocalizedString(@"Status of Selected Files", "Action menu -> status string toggle")];
            [menuItem setState: [fDefaults boolForKey: @"DisplayStatusProgressSelected"] ? NSOnState : NSOffState];
        }
        
        return [fWindow isVisible];
    }
    
    if (action == @selector(toggleAvailabilityBar:))
    {
        [menuItem setState: [fDefaults boolForKey: @"DisplayProgressBarAvailable"] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }
    
    if (action == @selector(setLimitGlobalEnabled:))
    {
        BOOL upload = [menuItem menu] == fUploadMenu;
        BOOL limit = menuItem == (upload ? fUploadLimitItem : fDownloadLimitItem);
        if (limit)
            [menuItem setTitle: [NSString stringWithFormat: NSLocalizedString(@"Limit (%d KB/s)",
                                    "Action menu -> upload/download limit"),
                                    [fDefaults integerForKey: upload ? @"UploadLimit" : @"DownloadLimit"]]];
        
        [menuItem setState: [fDefaults boolForKey: upload ? @"CheckUpload" : @"CheckDownload"] ? limit : !limit];
        return YES;
    }
    
    if (action == @selector(setRatioGlobalEnabled:))
    {
        BOOL check = menuItem == fCheckRatioItem;
        if (check)
            [menuItem setTitle: [NSString localizedStringWithFormat: NSLocalizedString(@"Stop at Ratio (%.2f)",
                                    "Action menu -> ratio stop"), [fDefaults floatForKey: @"RatioLimit"]]];
        
        [menuItem setState: [fDefaults boolForKey: @"RatioCheck"] ? check : !check];
        return YES;
    }

    //enable show info
    if (action == @selector(showInfo:))
    {
        NSString * title = [[fInfoController window] isVisible] ? NSLocalizedString(@"Hide Inspector", "View menu -> Inspector")
                            : NSLocalizedString(@"Show Inspector", "View menu -> Inspector");
        [menuItem setTitle: title];

        return YES;
    }
    
    //enable prev/next inspector tab
    if (action == @selector(setInfoTab:))
        return [[fInfoController window] isVisible];
    
    //enable toggle status bar
    if (action == @selector(toggleStatusBar:))
    {
        NSString * title = [fStatusBar isHidden] ? NSLocalizedString(@"Show Status Bar", "View menu -> Status Bar")
                            : NSLocalizedString(@"Hide Status Bar", "View menu -> Status Bar");
        [menuItem setTitle: title];

        return [fWindow isVisible];
    }
    
    //enable toggle filter bar
    if (action == @selector(toggleFilterBar:))
    {
        NSString * title = [fFilterBar isHidden] ? NSLocalizedString(@"Show Filter Bar", "View menu -> Filter Bar")
                            : NSLocalizedString(@"Hide Filter Bar", "View menu -> Filter Bar");
        [menuItem setTitle: title];

        return [fWindow isVisible];
    }
    
    //enable prev/next filter button
    if (action == @selector(switchFilter:))
        return [fWindow isVisible] && ![fFilterBar isHidden];
    
    //enable quicklook item
    if (action == @selector(toggleQuickLook:))
        return [[QuickLookController quickLook] canQuickLook];
    
    //enable reveal in finder
    if (action == @selector(revealFile:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;

    //enable remove items
    if (action == @selector(removeNoDelete:) || action == @selector(removeDeleteData:)
        || action == @selector(removeDeleteTorrent:) || action == @selector(removeDeleteDataAndTorrent:))
    {
        BOOL warning = NO,
            onlyDownloading = [fDefaults boolForKey: @"CheckRemoveDownloading"],
            canDelete = action != @selector(removeDeleteTorrent:) && action != @selector(removeDeleteDataAndTorrent:);
        
        for (Torrent * torrent in [fTableView selectedTorrents])
        {
            if (!warning && [torrent isActive])
            {
                warning = onlyDownloading ? ![torrent isSeeding] : YES;
                if (warning && canDelete)
                    break;
            }
            if (!canDelete && [torrent publicTorrent])
            {
                canDelete = YES;
                if (warning)
                    break;
            }
        }
    
        //append or remove ellipsis when needed
        NSString * title = [menuItem title], * ellipsis = [NSString ellipsis];
        if (warning && [fDefaults boolForKey: @"CheckRemove"])
        {
            if (![title hasSuffix: ellipsis])
                [menuItem setTitle: [title stringByAppendingEllipsis]];
        }
        else
        {
            if ([title hasSuffix: ellipsis])
                [menuItem setTitle: [title substringToIndex: [title rangeOfString: ellipsis].location]];
        }
        
        return canUseTable && canDelete && [fTableView numberOfSelectedRows] > 0;
    }

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
    {
        for (Torrent * torrent in fTorrents)
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
    {
        for (Torrent * torrent in fTorrents)
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume all waiting item
    if (action == @selector(resumeWaitingTorrents:))
    {
        if (![fDefaults boolForKey: @"Queue"] && ![fDefaults boolForKey: @"QueueSeed"])
            return NO;
    
        for (Torrent * torrent in fTorrents)
            if (![torrent isActive] && [torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume selected waiting item
    if (action == @selector(resumeSelectedTorrentsNoWait:))
    {
        if (!canUseTable)
            return NO;
        
        for (Torrent * torrent in [fTableView selectedTorrents])
            if (![torrent isActive])
                return YES;
        return NO;
    }

    //enable pause item
    if (action == @selector(stopSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;
    
        for (Torrent * torrent in [fTableView selectedTorrents])
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume item
    if (action == @selector(resumeSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;
    
        for (Torrent * torrent in [fTableView selectedTorrents])
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable manual announce item
    if (action == @selector(announceSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;
        
        for (Torrent * torrent in [fTableView selectedTorrents])
            if ([torrent canManualAnnounce])
                return YES;
        return NO;
    }
    
    //enable reset cache item
    if (action == @selector(verifySelectedTorrents:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    
    //enable move torrent file item
    if (action == @selector(moveDataFilesSelected:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    
    //enable copy torrent file item
    if (action == @selector(copyTorrentFiles:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    
    //enable reverse sort item
    if (action == @selector(setSortReverse:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SortReverse"] ? NSOnState : NSOffState];
        return ![[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER];
    }
    
    //enable group sort item
    if (action == @selector(setSortByGroup:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SortByGroup"] ? NSOnState : NSOffState];
        return YES;
    }
    
    //check proper filter search item
    if (action == @selector(setFilterSearchType:))
    {
        NSString * filterType = [fDefaults stringForKey: @"FilterSearchType"];
        
        BOOL state;
        if ([menuItem tag] == FILTER_TYPE_TAG_TRACKER)
            state = [filterType isEqualToString: FILTER_TYPE_TRACKER];
        else
            state = [filterType isEqualToString: FILTER_TYPE_NAME];
        
        [menuItem setState: state ? NSOnState : NSOffState];
        return YES;
    }
    
    return YES;
}

- (void) sleepCallback: (natural_t) messageType argument: (void *) messageArgument
{
    switch (messageType)
    {
        case kIOMessageSystemWillSleep:
            //if there are any running transfers, wait 15 seconds for them to stop
            for (Torrent * torrent in fTorrents)
                if ([torrent isActive])
                {
                    //stop all transfers (since some are active) before going to sleep and remember to resume when we wake up
                    [fTorrents makeObjectsPerformSelector: @selector(sleep)];
                    sleep(15);
                    break;
                }

            IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageCanSystemSleep:
            if ([fDefaults boolForKey: @"SleepPrevent"])
            {
                //prevent idle sleep unless no torrents are active
                for (Torrent * torrent in fTorrents)
                    if ([torrent isActive] && ![torrent isStalled] && ![torrent isError])
                    {
                        IOCancelPowerChange(fRootPort, (long) messageArgument);
                        return;
                    }
            }
            
            IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageSystemHasPoweredOn:
            //resume sleeping transfers after we wake up
            [fTorrents makeObjectsPerformSelector: @selector(wakeUp)];
            [self autoSpeedLimitChange: nil];
            break;
    }
}

- (NSMenu *) applicationDockMenu: (NSApplication *) sender
{
    NSInteger seeding = 0, downloading = 0;
    for (Torrent * torrent in fTorrents)
    {
        if ([torrent isSeeding])
            seeding++;
        else if ([torrent isActive])
            downloading++;
        else;
    }
    
    NSMenuItem * seedingItem = [fDockMenu itemWithTag: DOCK_SEEDING_TAG],
            * downloadingItem = [fDockMenu itemWithTag: DOCK_DOWNLOADING_TAG];
    
    BOOL hasSeparator = seedingItem || downloadingItem;
    
    if (seeding > 0)
    {
        NSString * title = [NSString stringWithFormat: NSLocalizedString(@"%d Seeding", "Dock item - Seeding"), seeding];
        if (!seedingItem)
        {
            seedingItem = [[[NSMenuItem alloc] initWithTitle: title action: nil keyEquivalent: @""] autorelease];
            [seedingItem setTag: DOCK_SEEDING_TAG];
            [fDockMenu insertItem: seedingItem atIndex: 0];
        }
        else
            [seedingItem setTitle: title];
    }
    else
    {
        if (seedingItem)
            [fDockMenu removeItem: seedingItem];
    }
    
    if (downloading > 0)
    {
        NSString * title = [NSString stringWithFormat: NSLocalizedString(@"%d Downloading", "Dock item - Downloading"), downloading];
        if (!downloadingItem)
        {
            downloadingItem = [[[NSMenuItem alloc] initWithTitle: title action: nil keyEquivalent: @""] autorelease];
            [downloadingItem setTag: DOCK_DOWNLOADING_TAG];
            [fDockMenu insertItem: downloadingItem atIndex: seeding > 0 ? 1 : 0];
        }
        else
            [downloadingItem setTitle: title];
    }
    else
    {
        if (downloadingItem)
            [fDockMenu removeItem: downloadingItem];
    }
    
    if (seeding > 0 || downloading > 0)
    {
        if (!hasSeparator)
            [fDockMenu insertItem: [NSMenuItem separatorItem] atIndex: seeding > 0 && downloading > 0 ? 2 : 1];
    }
    else
    {
        if (hasSeparator)
            [fDockMenu removeItemAtIndex: 0];
    }
    
    return fDockMenu;
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) window defaultFrame: (NSRect) defaultFrame
{
    //if auto size is enabled, the current frame shouldn't need to change
    NSRect frame = [fDefaults boolForKey: @"AutoSize"] ? [window frame] : [self sizedWindowFrame];
    
    frame.size.width = [fDefaults boolForKey: @"SmallView"] ? [fWindow minSize].width : WINDOW_REGULAR_WIDTH;
    return frame;
}

- (void) setWindowSizeToFit
{
    if ([fDefaults boolForKey: @"AutoSize"])
    {
        NSScrollView * scrollView = [fTableView enclosingScrollView];
        
        [scrollView setHasVerticalScroller: NO];
        [fWindow setFrame: [self sizedWindowFrame] display: YES animate: YES];
        [scrollView setHasVerticalScroller: YES];
        
        //hack to ensure scrollbars don't disappear after resizing
        [scrollView setAutohidesScrollers: NO];
        [scrollView setAutohidesScrollers: YES];
    }
}

- (NSRect) sizedWindowFrame
{
    NSInteger groups = ([fDisplayedTorrents count] > 0 && ![[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [Torrent class]])
                    ? [fDisplayedTorrents count] : 0;
    
    CGFloat heightChange = (GROUP_SEPARATOR_HEIGHT + [fTableView intercellSpacing].height) * groups
                        + ([fTableView rowHeight] + [fTableView intercellSpacing].height) * ([fTableView numberOfRows] - groups)
                        - [[fTableView enclosingScrollView] frame].size.height;
    
    return [self windowFrameByAddingHeight: heightChange checkLimits: YES];
}

- (void) updateForExpandCollape
{
    [self setWindowSizeToFit];
    [self setBottomCountText: YES];
}

- (void) showMainWindow: (id) sender
{
    [fWindow makeKeyAndOrderFront: nil];
}

- (void) windowDidBecomeMain: (NSNotification *) notification
{
    [fBadger clearCompleted];
    [self updateUI];
}

- (NSSize) windowWillResize: (NSWindow *) sender toSize: (NSSize) proposedFrameSize
{
    //only resize horizontally if autosize is enabled
    if ([fDefaults boolForKey: @"AutoSize"])
        proposedFrameSize.height = [fWindow frame].size.height;
    return proposedFrameSize;
}

- (void) windowDidResize: (NSNotification *) notification
{
    if (![fStatusBar isHidden])
        [self resizeStatusButton];
    
    if ([fFilterBar isHidden])
        return;
    
    //replace all buttons
    [fNoFilterButton sizeToFit];
    [fActiveFilterButton sizeToFit];
    [fDownloadFilterButton sizeToFit];
    [fSeedFilterButton sizeToFit];
    [fPauseFilterButton sizeToFit];
    
    NSRect allRect = [fNoFilterButton frame];
    NSRect activeRect = [fActiveFilterButton frame];
    NSRect downloadRect = [fDownloadFilterButton frame];
    NSRect seedRect = [fSeedFilterButton frame];
    NSRect pauseRect = [fPauseFilterButton frame];
    
    //size search filter to not overlap buttons
    NSRect searchFrame = [fSearchFilterField frame];
    searchFrame.origin.x = NSMaxX(pauseRect) + 5.0;
    searchFrame.size.width = [fStatusBar frame].size.width - searchFrame.origin.x - 5.0;
    
    //make sure it is not too long
    if (searchFrame.size.width > SEARCH_FILTER_MAX_WIDTH)
    {
        searchFrame.origin.x += searchFrame.size.width - SEARCH_FILTER_MAX_WIDTH;
        searchFrame.size.width = SEARCH_FILTER_MAX_WIDTH;
    }
    else if (searchFrame.size.width < SEARCH_FILTER_MIN_WIDTH)
    {
        searchFrame.origin.x += searchFrame.size.width - SEARCH_FILTER_MIN_WIDTH;
        searchFrame.size.width = SEARCH_FILTER_MIN_WIDTH;
        
        //calculate width the buttons can take up
        const CGFloat allowedWidth = (searchFrame.origin.x - 5.0) - allRect.origin.x;
        const CGFloat currentWidth = NSWidth(allRect) + NSWidth(activeRect) + NSWidth(downloadRect) + NSWidth(seedRect)
                                        + NSWidth(pauseRect) + 4.0; //add 4 for space between buttons
        const CGFloat ratio = allowedWidth / currentWidth;
        
        //decrease button widths proportionally
        allRect.size.width  = NSWidth(allRect) * ratio;
        activeRect.size.width = NSWidth(activeRect) * ratio;
        downloadRect.size.width = NSWidth(downloadRect) * ratio;
        seedRect.size.width = NSWidth(seedRect) * ratio;
        pauseRect.size.width = NSWidth(pauseRect) * ratio;
    }
    else;
    
    activeRect.origin.x = NSMaxX(allRect) + 1.0;
    downloadRect.origin.x = NSMaxX(activeRect) + 1.0;
    seedRect.origin.x = NSMaxX(downloadRect) + 1.0;
    pauseRect.origin.x = NSMaxX(seedRect) + 1.0;
    
    [fNoFilterButton setFrame: allRect];
    [fActiveFilterButton setFrame: activeRect];
    [fDownloadFilterButton setFrame: downloadRect];
    [fSeedFilterButton setFrame: seedRect];
    [fPauseFilterButton setFrame: pauseRect];
    
    [fSearchFilterField setFrame: searchFrame];
}

- (void) applicationWillUnhide: (NSNotification *) notification
{
    [self updateUI];
}

- (NSArray *) quickLookURLs
{
    NSArray * selectedTorrents = [fTableView selectedTorrents];
    NSMutableArray * urlArray = [NSMutableArray arrayWithCapacity: [selectedTorrents count]];
    
    for (Torrent * torrent in selectedTorrents)
        if ([self canQuickLookTorrent: torrent])
            [urlArray addObject: [NSURL fileURLWithPath: [torrent dataLocation]]];
    
    return urlArray;
}

- (BOOL) canQuickLook
{
    for (Torrent * torrent in [fTableView selectedTorrents])
        if ([self canQuickLookTorrent: torrent])
            return YES;
    
    return NO;
}

- (BOOL) canQuickLookTorrent: (Torrent *) torrent
{
    if (![[NSFileManager defaultManager] fileExistsAtPath: [torrent dataLocation]])
        return NO;
    
    return [torrent isFolder] || [torrent isComplete];
}

- (NSRect) quickLookFrameWithURL: (NSURL *) url
{
    if ([fWindow isVisible])
    {
        NSString * fullPath = [url path];
        NSRange visibleRows = [fTableView rowsInRect: [fTableView bounds]];
        
        for (NSInteger row = 0; row < NSMaxRange(visibleRows); row++)
        {
            id item = [fTableView itemAtRow: row];
            if ([item isKindOfClass: [Torrent class]] && [[(Torrent *)item dataLocation] isEqualToString: fullPath])
            {
                NSRect frame = [fTableView iconRectForRow: row];
                frame.origin = [fTableView convertPoint: frame.origin toView: nil];
                frame.origin = [fWindow convertBaseToScreen: frame.origin];
                frame.origin.y -= frame.size.height;
                return frame;
            }
        }
    }
    
    return NSZeroRect;
}

- (void) toggleQuickLook: (id) sender
{
    [[QuickLookController quickLook] toggleQuickLook];
}

- (void) linkHomepage: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: WEBSITE_URL]];
}

- (void) linkForums: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: FORUM_URL]];
}

- (void) linkTrac: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: TRAC_URL]];
}

- (void) linkDonate: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: DONATE_URL]];
}

- (void) updaterWillRelaunchApplication: (SUUpdater *) updater
{
    fUpdateInProgress = YES;
}

- (NSDictionary *) registrationDictionaryForGrowl
{
    NSArray * notifications = [NSArray arrayWithObjects: GROWL_DOWNLOAD_COMPLETE, GROWL_SEEDING_COMPLETE,
                                                            GROWL_AUTO_ADD, GROWL_AUTO_SPEED_LIMIT, nil];
    return [NSDictionary dictionaryWithObjectsAndKeys: notifications, GROWL_NOTIFICATIONS_ALL,
                                notifications, GROWL_NOTIFICATIONS_DEFAULT, nil];
}

- (void) growlNotificationWasClicked: (id) clickContext
{
    if (!clickContext || ![clickContext isKindOfClass: [NSDictionary class]])
        return;
    
    NSString * type = [clickContext objectForKey: @"Type"], * location;
    if (([type isEqualToString: GROWL_DOWNLOAD_COMPLETE] || [type isEqualToString: GROWL_SEEDING_COMPLETE])
            && (location = [clickContext objectForKey: @"Location"]))
        [[NSWorkspace sharedWorkspace] selectFile: location inFileViewerRootedAtPath: nil];
}

- (void) rpcCallback: (tr_rpc_callback_type) type forTorrentStruct: (struct tr_torrent *) torrentStruct
{
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    
    //get the torrent
    Torrent * torrent = nil;
    if (torrentStruct != NULL && (type != TR_RPC_TORRENT_ADDED && type != TR_RPC_SESSION_CHANGED))
    {
        for (torrent in fTorrents)
            if (torrentStruct == [torrent torrentStruct])
            {
                [torrent retain];
                break;
            }
        
        if (!torrent)
        {
            [pool release];
            
            NSLog(@"No torrent found matching the given torrent struct from the RPC callback!");
            return;
        }
    }
    
    switch (type)
    {
        case TR_RPC_TORRENT_ADDED:
            [self performSelectorOnMainThread: @selector(rpcAddTorrentStruct:) withObject:
                [[NSValue valueWithPointer: torrentStruct] retain] waitUntilDone: NO];
            break;
        
        case TR_RPC_TORRENT_STARTED:
        case TR_RPC_TORRENT_STOPPED:
            [self performSelectorOnMainThread: @selector(rpcStartedStoppedTorrent:) withObject: torrent waitUntilDone: NO];
            break;
        
        case TR_RPC_TORRENT_REMOVING:
            [self performSelectorOnMainThread: @selector(rpcRemoveTorrent:) withObject: torrent waitUntilDone: NO];
            break;
        
        case TR_RPC_TORRENT_CHANGED:
            [self performSelectorOnMainThread: @selector(rpcChangedTorrent:) withObject: torrent waitUntilDone: NO];
            break;
        
        case TR_RPC_SESSION_CHANGED:
            [fPrefsController performSelectorOnMainThread: @selector(rpcUpdatePrefs) withObject: nil waitUntilDone: NO];
            break;
        
        default:
            NSLog(@"Unknown RPC command received!");
            [torrent release];
    }
    
    [pool release];
}

- (void) rpcAddTorrentStruct: (NSValue *) torrentStructPtr
{
    tr_torrent * torrentStruct = (tr_torrent *)[torrentStructPtr pointerValue];
    [torrentStructPtr release];
    
    NSString * location = nil;
    if (tr_torrentGetDownloadDir(torrentStruct) != NULL)
        location = [NSString stringWithUTF8String: tr_torrentGetDownloadDir(torrentStruct)];
    
    Torrent * torrent = [[Torrent alloc] initWithTorrentStruct: torrentStruct location: location lib: fLib];
    
    [torrent update];
    [fTorrents addObject: torrent];
    [torrent release];
    
    [self updateTorrentsInQueue];
}

- (void) rpcRemoveTorrent: (Torrent *) torrent
{
    [self confirmRemoveTorrents: [[NSArray arrayWithObject: torrent] retain] deleteData: NO deleteTorrent: NO];
    [torrent release];
}

- (void) rpcStartedStoppedTorrent: (Torrent *) torrent
{
    [torrent update];
    [torrent release];
    
    [self updateUI];
    [self applyFilter: nil];
    [self updateTorrentHistory];
}

- (void) rpcChangedTorrent: (Torrent *) torrent
{
    [torrent update];
    
    if ([[fTableView selectedTorrents] containsObject: torrent])
    {
        [fInfoController updateInfoStats]; //this will reload the file table
        [fInfoController updateOptions];
    }
    
    [torrent release];
}

@end
