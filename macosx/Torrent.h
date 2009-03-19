/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2009 Transmission authors and contributors
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

#import <Cocoa/Cocoa.h>
#import <transmission.h>

@class FileListNode;

typedef enum
{
    TORRENT_FILE_DELETE,
    TORRENT_FILE_SAVE,
    TORRENT_FILE_DEFAULT
} torrentFileState;

#define STAT_TIME_NONE -1
#define STAT_TIME_NOW -2

@interface Torrent : NSObject
{
    tr_torrent * fHandle;
    const tr_info * fInfo;
    const tr_stat * fStat;
    
    BOOL fResumeOnWake;
    
    BOOL fUseIncompleteFolder;
    NSString * fDownloadFolder, * fIncompleteFolder;
    
    BOOL fPublicTorrent;
    NSString * fPublicTorrentLocation;
	
    NSUserDefaults * fDefaults;

    NSImage * fIcon;
    
    NSString * fNameString, * fHashString;
    
    tr_file_stat * fFileStat;
    NSArray * fFileList, * fFlatFileList;
    
    NSIndexSet * fPreviousFinishedIndexes;
    NSDate * fPreviousFinishedIndexesDate;
    
    BOOL fFinishedSeeding, fWaitToStart, fStalled;
    
    NSInteger fGroupValue;
    
    BOOL fAddedTrackers;
    
    NSDictionary * fQuickPauseDict;
}

- (id) initWithPath: (NSString *) path location: (NSString *) location deleteTorrentFile: (torrentFileState) torrentDelete
        lib: (tr_session *) lib;
- (id) initWithTorrentStruct: (tr_torrent *) torrentStruct location: (NSString *) location lib: (tr_session *) lib;
- (id) initWithHistory: (NSDictionary *) history lib: (tr_session *) lib;

- (NSDictionary *) history;

- (void) closeRemoveTorrent;

- (void) changeIncompleteDownloadFolder: (NSString *) folder;
- (void) changeDownloadFolder: (NSString *) folder;
- (NSString *) downloadFolder;

- (void) getAvailability: (int8_t *) tab size: (NSInteger) size;
- (void) getAmountFinished: (float *) tab size: (NSInteger) size;
- (NSIndexSet *) previousFinishedPieces;
-(void) setPreviousFinishedPieces: (NSIndexSet *) indexes;

- (void) update;

- (void) startTransfer;
- (void) stopTransfer;
- (void) sleep;
- (void) wakeUp;

- (void) manualAnnounce;
- (BOOL) canManualAnnounce;

- (void) resetCache;

- (CGFloat) ratio;
- (tr_ratiolimit) ratioSetting;
- (void) setRatioSetting: (tr_ratiolimit) setting;
- (CGFloat) ratioLimit;
- (void) setRatioLimit: (CGFloat) limit;
- (BOOL) seedRatioSet;
- (CGFloat) progressStopRatio;

- (BOOL) usesSpeedLimit: (BOOL) upload;
- (void) setUseSpeedLimit: (BOOL) use upload: (BOOL) upload;
- (NSInteger) speedLimit: (BOOL) upload;
- (void) setSpeedLimit: (NSInteger) limit upload: (BOOL) upload;
- (BOOL) usesGlobalSpeedLimit;
- (void) setUseGlobalSpeedLimit: (BOOL) use;

- (void) setMaxPeerConnect: (uint16_t) count;
- (uint16_t) maxPeerConnect;

- (void) setWaitToStart: (BOOL) wait;
- (BOOL) waitingToStart;

- (void) revealData;
- (void) revealPublicTorrent;
- (void) trashData;
- (void) trashTorrent;
- (void) moveTorrentDataFileTo: (NSString *) folder;
- (void) copyTorrentFileTo: (NSString *) path;

- (BOOL) alertForRemainingDiskSpace;
- (BOOL) alertForFolderAvailable;
- (BOOL) alertForMoveFolderAvailable;

- (NSImage *) icon;

- (NSString *) name;
- (BOOL) isFolder;
- (uint64_t) size;
- (uint64_t) sizeLeft;

- (NSString *) trackerAddressAnnounce;
- (NSDate *) lastAnnounceTime;
- (NSInteger) nextAnnounceTime;
- (NSString *) announceResponse;

- (NSString *) trackerAddressScrape;
- (NSDate *) lastScrapeTime;
- (NSInteger) nextScrapeTime;
- (NSString *) scrapeResponse;

- (NSMutableArray *) allTrackers: (BOOL) separators;
- (NSArray *) allTrackersFlat;
- (BOOL) updateAllTrackersForAdd: (NSMutableArray *) trackers;
- (void) updateAllTrackersForRemove: (NSMutableArray *) trackers;
- (BOOL) hasAddedTrackers;

- (NSString *) comment;
- (NSString *) creator;
- (NSDate *) dateCreated;

- (NSInteger) pieceSize;
- (NSInteger) pieceCount;
- (NSString *) hashString;
- (BOOL) privateTorrent;

- (NSString *) torrentLocation;
- (NSString *) publicTorrentLocation;
- (NSString *) dataLocation;

- (BOOL) publicTorrent;

- (CGFloat) progress;
- (CGFloat) progressDone;
- (CGFloat) progressLeft;
- (CGFloat) checkingProgress;

- (NSInteger) eta;

- (CGFloat) notAvailableDesired;

- (BOOL) isActive;
- (BOOL) isSeeding;
- (BOOL) isChecking;
- (BOOL) isCheckingWaiting;
- (BOOL) allDownloaded;
- (BOOL) isComplete;
- (BOOL) isError;
- (NSString *) errorMessage;

- (NSArray *) peers;

- (NSUInteger) webSeedCount;
- (NSArray *) webSeeds;

- (NSString *) progressString;
- (NSString *) statusString;
- (NSString *) shortStatusString;
- (NSString *) remainingTimeString;

- (NSString *) stateString;

- (NSInteger) seeders;
- (NSInteger) leechers;
- (NSInteger) completedFromTracker;

- (NSInteger) totalPeersConnected;
- (NSInteger) totalPeersTracker;
- (NSInteger) totalPeersIncoming;
- (NSInteger) totalPeersCache;
- (NSInteger) totalPeersPex;
- (NSInteger) totalPeersKnown;

- (NSInteger) peersSendingToUs;
- (NSInteger) peersGettingFromUs;

- (CGFloat) downloadRate;
- (CGFloat) uploadRate;
- (CGFloat) totalRate;
- (uint64_t) haveVerified;
- (uint64_t) haveTotal;
- (uint64_t) totalSizeSelected;
- (uint64_t) downloadedTotal;
- (uint64_t) uploadedTotal;
- (uint64_t) failedHash;
- (CGFloat) swarmSpeed;

- (NSInteger) groupValue;
- (void) setGroupValue: (NSInteger) groupValue;
- (NSInteger) groupOrderValue;
- (void) checkGroupValueForRemoval: (NSNotification *) notification;

- (NSArray *) fileList;
- (NSInteger) fileCount;
- (void) updateFileStat;
- (NSArray *) flatFileList;

//methods require fileStats to have been updated recently to be accurate
- (CGFloat) fileProgress: (FileListNode *) node;
- (BOOL) canChangeDownloadCheckForFile: (NSInteger) index;
- (BOOL) canChangeDownloadCheckForFiles: (NSIndexSet *) indexSet;
- (NSInteger) checkForFiles: (NSIndexSet *) indexSet;
- (void) setFileCheckState: (NSInteger) state forIndexes: (NSIndexSet *) indexSet;
- (void) setFilePriority: (NSInteger) priority forIndexes: (NSIndexSet *) indexSet;
- (BOOL) hasFilePriority: (NSInteger) priority forIndexes: (NSIndexSet *) indexSet;
- (NSSet *) filePrioritiesForIndexes: (NSIndexSet *) indexSet;

- (NSDate *) dateAdded;
- (NSDate *) dateCompleted;
- (NSDate *) dateActivity;
- (NSDate *) dateActivityOrAdd;

- (NSInteger) stalledMinutes;
- (BOOL) isStalled;

- (NSInteger) stateSortKey;

- (tr_torrent *) torrentStruct;

@end
