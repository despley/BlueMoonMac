/**
 * @file
 * Interface of class BMCardSelectionSheet.
 */

#import <Cocoa/Cocoa.h>
#import "bluemoon.h"

/**
 * This sheet is used to choose cards to discard, draw, etc. whenever 
 * requested by the game engine. The sheet is loaded from CardSelectionSheet.nib.
 */
@interface BMCardSelectionSheet : NSObject 
{
    // Views and layers
    IBOutlet NSWindow *sheet;           ///< Pointer to the sheet
    IBOutlet NSView *cardPreviewView;   ///< View used to show card preview
    CALayer *previewLayer;              ///< Layer used to show card preview
    
    /**
     * Array controller with information about the cards shown in the sheet.
     * The array is initialized in setChoices:amount: and it contains 
     * mutable dictionaries with the follwing keys:
     *     name     Name of the card (NSString)
     *     design   Pointer to design of the card (NSValue)
     *     selected Is the card selected or not (BOOL)
     */
    IBOutlet NSArrayController *choiceArrayController;

    // Properties
    choose_result callback;     ///< Callback function used to report back to engine
    int minAmount;              ///< User must select at least this many cards
    int maxAmount;              ///< User must select at most this many cards
    NSString *prompt;           ///< Text with instructions to the user
    game *theGame;              ///< Pointer to game engine data
    int who;                    ///< Data received from the game engine - must be passed back unchanged.
    void *dataPointer;          ///< Pointer to data received from game engine - must be passed back unchanged.
    BOOL endTurnAfter;          ///< If this is set to YES, game turn is ended automatically after the sheet is closed.
    
    NSArray *cardSelections;
}

@property choose_result callback;
@property int minAmount;
@property int maxAmount;
@property(copy) NSString *prompt;
@property game *theGame;
@property int who;
@property void *dataPointer;
@property BOOL endTurnAfter;

+ (BMCardSelectionSheet *)sheet;
- (void)free;
- (void)display;
- (void)setChoices:(struct design **)choices amount:(int)amount;

- (IBAction)closeButtonWasClicked:(id)sender;

@end
