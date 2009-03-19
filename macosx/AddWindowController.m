/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008-2009 Transmission authors and contributors
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

#import "AddWindowController.h"
#import "FileOutlineController.h"
#import "Controller.h"
#import "GroupsController.h"
#import "NSStringAdditions.h"
#import "ExpandedPathToIconTransformer.h"

#define UPDATE_SECONDS 1.0

@interface AddWindowController (Private)

- (void) confirmAdd;

- (void) setDestinationPath: (NSString *) destination;

- (void) folderChoiceClosed: (NSOpenPanel *) openPanel returnCode: (NSInteger) code contextInfo: (void *) contextInfo;

- (void) setGroupsMenu;
- (void) changeGroupValue: (id) sender;

- (void) sameNameAlertDidEnd: (NSAlert *) alert returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo;

@end

@implementation AddWindowController

- (id) initWithTorrent: (Torrent *) torrent destination: (NSString *) path lockDestination: (BOOL) lockDestination
    controller: (Controller *) controller deleteTorrent: (torrentFileState) deleteTorrent
{
    if ((self = [super initWithWindowNibName: @"AddWindow"]))
    {
        fTorrent = torrent;
        if (path)
            fDestination = [[path stringByExpandingTildeInPath] retain];
        fLockDestination = lockDestination;
        
        fController = controller;
        
        fDeleteTorrent = deleteTorrent == TORRENT_FILE_DELETE || (deleteTorrent == TORRENT_FILE_DEFAULT
                            && [[NSUserDefaults standardUserDefaults] boolForKey: @"DeleteOriginalTorrent"]);
        fDeleteEnable = deleteTorrent == TORRENT_FILE_DEFAULT;
        
        fGroupValue = [torrent groupValue];
    }
    return self;
}

- (void) awakeFromNib
{
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateStatusField:)
        name: @"TorrentFileCheckChange" object: fTorrent];
    
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateGroupMenu:)
        name: @"UpdateGroups" object: nil];
    
    [fFileController setTorrent: fTorrent];
    
    NSString * name = [fTorrent name];
    [[self window] setTitle: name];
    [fNameField setStringValue: name];
    
    NSImage * icon = [[fTorrent icon] copy];
    [icon setFlipped: NO];
    [fIconView setImage: icon];
    [icon release];
    
    [self updateStatusField: nil];
    
    [self setGroupsMenu];
    [fGroupPopUp selectItemWithTag: fGroupValue];
    
    [fStartCheck setState: [[NSUserDefaults standardUserDefaults] boolForKey: @"AutoStartDownload"] ? NSOnState : NSOffState];
    
    [fDeleteCheck setState: fDeleteTorrent ? NSOnState : NSOffState];
    [fDeleteCheck setEnabled: fDeleteEnable];
    
    if (fDestination)
        [self setDestinationPath: fDestination];
    else
    {
        [fLocationField setStringValue: @""];
        [fLocationImageView setImage: nil];
    }
    
    fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_SECONDS target: fFileController
                selector: @selector(reloadData) userInfo: nil repeats: YES];
}

- (void) windowDidLoad
{
    //if there is no destination, prompt for one right away
    if (!fDestination)
        [self setDestination: nil];
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fTimer invalidate];
    
    [fDestination release];
    
    [super dealloc];
}

- (Torrent *) torrent
{
    return fTorrent;
}

- (void) setDestination: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Open torrent -> prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];
    
    [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the download folder for \"%@\"",
                        "Add -> select destination folder"), [fTorrent name]]];
    
    [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(folderChoiceClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) add: (id) sender
{
    if ([[fDestination lastPathComponent] isEqualToString: [fTorrent name]]
        && [[NSUserDefaults standardUserDefaults] boolForKey: @"WarningFolderDataSameName"])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: NSLocalizedString(@"The destination directory and root data directory have the same name.",
                                "Add torrent -> same name -> title")];
        [alert setInformativeText: NSLocalizedString(@"If you are attempting to use already existing data,"
            " the root data directory should be inside the destination directory.", "Add torrent -> same name -> message")];
        [alert setAlertStyle: NSWarningAlertStyle];
        [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Add torrent -> same name -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Add", "Add torrent -> same name -> button")];
        [alert setShowsSuppressionButton: YES];
        
        [alert beginSheetModalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(sameNameAlertDidEnd:returnCode:contextInfo:) contextInfo: nil];
    }
    else
        [self confirmAdd];
}

- (void) cancelAdd: (id) sender
{
    [[self window] performClose: sender];
}

//only called on cancel
- (BOOL) windowShouldClose: (id) window
{
    [fTimer invalidate];
    fTimer = nil;
    
    [fFileController setTorrent: nil]; //avoid a crash when window tries to update
    
    [fController askOpenConfirmed: self add: NO];
    return YES;
}

- (void) verifyLocalData: (id) sender
{
    [fTorrent resetCache];
    [fFileController reloadData];
}

- (void) updateStatusField: (NSNotification *) notification
{
    NSString * statusString = [NSString stringForFileSize: [fTorrent size]];
    if ([fTorrent isFolder])
    {
        NSString * fileString;
        NSInteger count = [fTorrent fileCount];
        if (count != 1)
            fileString = [NSString stringWithFormat: NSLocalizedString(@"%d files", "Add torrent -> info"), count];
        else
            fileString = NSLocalizedString(@"1 file", "Add torrent -> info");
        
        NSString * selectedString = [NSString stringWithFormat: NSLocalizedString(@"%@ selected", "Add torrent -> info"),
                                        [NSString stringForFileSize: [fTorrent totalSizeSelected]]];
        
        statusString = [NSString stringWithFormat: @"%@, %@ (%@)", fileString, statusString, selectedString];
    }
    
    [fStatusField setStringValue: statusString];
}

- (void) updateGroupMenu: (NSNotification *) notification
{
    [self setGroupsMenu];
    if (![fGroupPopUp selectItemWithTag: fGroupValue])
    {
        fGroupValue = -1;
        [fGroupPopUp selectItemWithTag: fGroupValue];
    }
}

@end

@implementation AddWindowController (Private)

- (void) confirmAdd
{
    [fTimer invalidate];
    fTimer = nil;
    
    [fTorrent setWaitToStart: [fStartCheck state] == NSOnState];
    [fTorrent setGroupValue: fGroupValue];
    
    if ([fDeleteCheck state] == NSOnState)
        [fTorrent trashTorrent];
    
    [fFileController setTorrent: nil]; //avoid a crash when window tries to update
    
    [self close];
    [fController askOpenConfirmed: self add: YES]; //ensure last, since it releases this controller
}

- (void) setDestinationPath: (NSString *) destination
{
    destination = [destination stringByExpandingTildeInPath];
    if (!fDestination || ![fDestination isEqualToString: destination])
    { 
        [fDestination release];
        fDestination = [destination retain];
        
        [fTorrent changeDownloadFolder: fDestination];
    }
    
    [fLocationField setStringValue: [fDestination stringByAbbreviatingWithTildeInPath]];
    [fLocationField setToolTip: fDestination];
    
    ExpandedPathToIconTransformer * iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
    [fLocationImageView setImage: [iconTransformer transformedValue: fDestination]];
    [iconTransformer release];
}

- (void) folderChoiceClosed: (NSOpenPanel *) openPanel returnCode: (NSInteger) code contextInfo: (void *) contextInfo
{
    if (code == NSOKButton)
    {
        fLockDestination = NO;
        [self setDestinationPath: [[openPanel filenames] objectAtIndex: 0]];
    }
    else
    {
        if (!fDestination)
            [self performSelectorOnMainThread: @selector(cancelAdd:) withObject: nil waitUntilDone: NO];
    }
}

- (void) setGroupsMenu
{
    NSMenu * groupMenu = [[GroupsController groups] groupMenuWithTarget: self action: @selector(changeGroupValue:) isSmall: NO];
    [fGroupPopUp setMenu: groupMenu];
}

- (void) changeGroupValue: (id) sender
{
    NSInteger previousGroup = fGroupValue;
    fGroupValue = [sender tag];
    
    if (!fLockDestination)
    {
        if ([[GroupsController groups] usesCustomDownloadLocationForIndex: fGroupValue])
            [self setDestinationPath: [[GroupsController groups] customDownloadLocationForIndex: fGroupValue]];
        else if ([fDestination isEqualToString: [[GroupsController groups] customDownloadLocationForIndex: previousGroup]])
            [self setDestinationPath: [[NSUserDefaults standardUserDefaults] stringForKey: @"DownloadFolder"]];
        else;
    }
}

- (void) sameNameAlertDidEnd: (NSAlert *) alert returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo
{
    if ([[alert suppressionButton] state] == NSOnState)
        [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningFolderDataSameName"];
    
    [alert release];
    
    if (returnCode == NSAlertSecondButtonReturn)
        [self performSelectorOnMainThread: @selector(confirmAdd) withObject: nil waitUntilDone: NO];
}

@end
