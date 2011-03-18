/**
 * @file
 * Interface of class BMPreferencesWindowController.
 */
 
#import <Cocoa/Cocoa.h>

@class BMCardImageStore;

@interface BMPreferencesWindowController : NSWindowController
{
    IBOutlet NSToolbar *toolbar;
    IBOutlet NSTabView *tabView;    
}

@property(readonly) BMCardImageStore *imageStore;

- (IBAction)toolbarItemWasSelected:(id)sender;

@end
