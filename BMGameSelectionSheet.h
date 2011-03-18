/**
 * @file
 * Interface of class BMGameSelectionSheet.
 */

#import <Cocoa/Cocoa.h>


@interface BMGameSelectionSheet : NSObject 
{
    IBOutlet NSWindow *sheet;
    
    unsigned int randomSeed;
    int computerDeck;
    int humanDeck;
    int startPlayer;
}

@property NSWindow *sheet;
@property unsigned int randomSeed;
@property int computerDeck;
@property int humanDeck;
@property int startPlayer;

+ (BMGameSelectionSheet *)sheet;
- (IBAction)closeButtonWasClicked:(id)sender;

@end
