/**
 * @file
 * Implementation of class BMPreferencesWindowController.
 */

#import "BMPreferencesWindowController.h"
#import "BMCardImageStore.h"

@implementation BMPreferencesWindowController

#pragma mark Properties

- (BMCardImageStore *)imageStore
{
    return [BMCardImageStore imageStore];
}

#pragma mark Public methods

- (void)awakeFromNib
{
    // Select the first toolbar item by default
    [toolbar setSelectedItemIdentifier:[[[toolbar items] objectAtIndex:0] itemIdentifier]];
}

- (NSArray *)toolbarSelectableItemIdentifiers:(NSToolbar *)theToolbar
{
    NSMutableArray *array = [NSMutableArray arrayWithCapacity:[[toolbar items] count]];
    for (NSToolbarItem *item in [toolbar items])
        [array addObject:[item itemIdentifier]];
    return array;
}

- (IBAction)toolbarItemWasSelected:(id)sender
{
    [tabView selectTabViewItemAtIndex:[sender tag]];
    NSString *windowTitle = NSLocalizedString(@"Preferences", @"Title of the Preferences window");
    [[self window] setTitle:[NSString stringWithFormat:@"&@ - %@", windowTitle, [sender label]]];
}

@end
