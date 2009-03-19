/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007-2009 Transmission authors and contributors
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

#import "FilePriorityCell.h"
#import "FileOutlineView.h"
#import "FileListNode.h"
#import "Torrent.h"

@implementation FilePriorityCell

- (id) init
{
    if ((self = [super init]))
    {
        [self setTrackingMode: NSSegmentSwitchTrackingSelectAny];
        [self setControlSize: NSMiniControlSize];
        [self setSegmentCount: 3];
        
        for (NSInteger i = 0; i < [self segmentCount]; i++)
        {
            [self setLabel: @"" forSegment: i];
            [self setWidth: 9.0f forSegment: i]; //9 is minimum size to get proper look
        }
        
        [self setImage: [NSImage imageNamed: @"PriorityControlLow.png"] forSegment: 0];
        [self setImage: [NSImage imageNamed: @"PriorityControlNormal.png"] forSegment: 1];
        [self setImage: [NSImage imageNamed: @"PriorityControlHigh.png"] forSegment: 2];
        
        fHoverRow = NO;
    }
    return self;
}

- (void) setSelected: (BOOL) flag forSegment: (NSInteger) segment
{
    [super setSelected: flag forSegment: segment];
    
    //only for when clicking manually
    NSInteger priority;
    switch (segment)
    {
        case 0:
            priority = TR_PRI_LOW;
            break;
        case 1:
            priority = TR_PRI_NORMAL;
            break;
        case 2:
            priority = TR_PRI_HIGH;
            break;
    }
    
    FileOutlineView * controlView = (FileOutlineView *)[self controlView];
    Torrent * torrent = [controlView torrent];
    [torrent setFilePriority: priority forIndexes: [(FileListNode *)[self representedObject] indexes]];
    [controlView reloadData];
}

- (void) addTrackingAreasForView: (NSView *) controlView inRect: (NSRect) cellFrame withUserInfo: (NSDictionary *) userInfo
            mouseLocation: (NSPoint) mouseLocation
{
    NSTrackingAreaOptions options = NSTrackingEnabledDuringMouseDrag | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways;
    
    if (NSMouseInRect(mouseLocation, cellFrame, [controlView isFlipped]))
    {
        options |= NSTrackingAssumeInside;
        [controlView setNeedsDisplayInRect: cellFrame];
    }
    
    NSTrackingArea * area = [[NSTrackingArea alloc] initWithRect: cellFrame options: options owner: controlView userInfo: userInfo];
    [controlView addTrackingArea: area];
    [area release];
}

- (void) setHovered: (BOOL) hovered
{
    fHoverRow = hovered;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    Torrent * torrent = [(FileOutlineView *)controlView torrent];
    FileListNode * node = [self representedObject];
    NSSet * priorities = [torrent filePrioritiesForIndexes: [node indexes]];
    
    const NSUInteger count = [priorities count];
    if (fHoverRow && count > 0)
    {
        [super setSelected: [priorities containsObject: [NSNumber numberWithInteger: TR_PRI_LOW]] forSegment: 0];
        [super setSelected: [priorities containsObject: [NSNumber numberWithInteger: TR_PRI_NORMAL]] forSegment: 1];
        [super setSelected: [priorities containsObject: [NSNumber numberWithInteger: TR_PRI_HIGH]] forSegment: 2];
        
        [super drawWithFrame: cellFrame inView: controlView];
    }
    else
    {
        NSImage * image;
        if (count == 0)
            image = [NSImage imageNamed: @"PriorityNone.png"];
        else if (count > 1)
            image = [NSImage imageNamed: @"PriorityMixed.png"];
        else
        {
            switch ([[priorities anyObject] integerValue])
            {
                case TR_PRI_NORMAL:
                    image = [NSImage imageNamed: @"PriorityNormal.png"];
                    break;
                case TR_PRI_LOW:
                    image = [NSImage imageNamed: @"PriorityLow.png"];
                    break;
                case TR_PRI_HIGH:
                    image = [NSImage imageNamed: @"PriorityHigh.png"];
                    break;
            }
        }
        
        NSSize imageSize = [image size];
        [image compositeToPoint: NSMakePoint(cellFrame.origin.x + (cellFrame.size.width - imageSize.width) * 0.5f,
                cellFrame.origin.y + (cellFrame.size.height + imageSize.height) * 0.5f) operation: NSCompositeSourceOver];
    }
}

@end
