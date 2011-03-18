/**
 * @file
 * Implementation of BMCardSelectionSheet.
 */

#import "BMCardSelectionSheet.h"
#import "BMMainWindowController.h"
#import "BMCardImageStore.h"

/// Global instance of the sheet, which can be created with +sheet and
/// destroyed with -free.
static BMCardSelectionSheet *s_instance = nil;

/// Private methods of BMCardSelectionSheet
@interface BMCardSelectionSheet ()
- (id)init;
- (BOOL)performCallback:(BOOL)simulate;
@end

@implementation BMCardSelectionSheet

@synthesize callback, minAmount, maxAmount, prompt, theGame, who, dataPointer, endTurnAfter;

/// Here we add a key-value observing dependecy needed to enable/disable
/// state of the close button.
+ (void)initialize
{
    [self setKeys:[NSArray arrayWithObject:@"cardSelections"] triggerChangeNotificationsForDependentKey:@"closingIsAllowed"];
}

/// Returns an instance of the card selection sheet.
+ (BMCardSelectionSheet *)sheet
{
    if (!s_instance)
        s_instance = [[[self class] alloc] init];
    return s_instance;
}

/// Frees the global singleton instance.
- (void)free
{    
    if (self == s_instance)
        s_instance = NULL;
}

/**
 * Checks if user has selected acceptable cards. Amount of selected cards
 * must be between minAmount and maxAmount, and the game engine is used to
 * check that other requirements are fulfilled.
 *
 * @return YES if selected cards are accepted, else NO.
 */
- (BOOL)closingIsAllowed
{
    int selectedCount = [[cardSelections filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"self == YES"]] count];
    return selectedCount >= minAmount && selectedCount <= maxAmount && [self performCallback:YES];
}

/// Displays the sheet.
- (void)display
{
    // Bind cardSelections array to choiceArrayController so we get notified
    // when user selects/deselects cards. This will also automatically
    // trigger change notification for dependent key "closingIsAllowed".
    [self bind:@"cardSelections" toObject:choiceArrayController withKeyPath:@"arrangedObjects.selected" options:nil];

    // Add observer for selection in the table view. This will update the
    // preview image whenever needed.
    [choiceArrayController addObserver:self forKeyPath:@"selection" options:NSKeyValueObservingOptionNew context:NULL];

    design *d_ptr = [[[[choiceArrayController arrangedObjects] objectAtIndex:0] valueForKey:@"design"] pointerValue];
    previewLayer.contents = (id)[[BMCardImageStore imageStore] imageForDesign:d_ptr];
    previewLayer.hidden = NO;    
    [NSApp beginSheet:sheet modalForWindow:[NSApp mainWindow] modalDelegate:nil didEndSelector:nil contextInfo:NULL];
}

/// Closes the sheet and handles the operation when user clicks the close button.
- (IBAction)closeButtonWasClicked:(id)sender
{
    [self unbind:@"cardSelections"];
    [choiceArrayController removeObserver:self forKeyPath:@"selection"];   

    // Close the sheet
    [sheet orderOut:sender];
    [NSApp endSheet:sheet];

    // Hide card preview so previous card won't be visible when the sheet is reopened
    previewLayer.hidden = YES;

    // Allow game engine to handle the operation
    [self performCallback:NO];

    if (endTurnAfter)
    {
        // Let AI take turn 
        endTurnAfter = NO;
        [[NSApp delegate] handleEndTurn];
    }
    else
    {
        // Update the user interface
        [[NSApp delegate] updateButtons];
        [[NSApp delegate] updateAll];
    }
}

/// Stores an array of cards provided by the game engine in choiceArrayController.
- (void)setChoices:(design **)choices amount:(int)amount
{
    /// Create a temporary array and store all information that we're going to need as NSMutableDictionaries
    NSMutableArray *array = [NSMutableArray array];
    for (int i = 0; i < amount; i++)
    {
        design *d_ptr = choices[i];
        [array addObject:[NSMutableDictionary dictionaryWithObjectsAndKeys:
            [NSString stringWithUTF8String:d_ptr->name], @"name",
            [NSValue valueWithPointer:d_ptr], @"design",
            [NSNumber numberWithBool:NO], @"selected",
            nil]];
    }

    /// Update contents of choiceArrayController
    [choiceArrayController removeObjects:[choiceArrayController arrangedObjects]];
    [choiceArrayController addObjects:array];
    [choiceArrayController setSelectionIndex:0];
}

#pragma mark Private methods

/// Initializes the sheet and loads CardSelectionSheet.nib bundle.
- (id)init
{
    if ((self = [super init]))
    {
        if (![NSBundle loadNibNamed:@"CardSelectionSheet" owner:self] || !sheet)
        {
            NSAssert(NO, @"[BMCardScelectionSheet init] Failed to load \"CardSelectionSheet.nib\"");
            return nil;
        }
        
        /// Create a layer for the card preview. Set size of the layer so it
        /// can show a card in full size and set the layer to have a shadow.
        [cardPreviewView setWantsLayer:YES];
        previewLayer = [CALayer layer];
        previewLayer.frame = CGRectMake(12, 18, 230, 409);
        previewLayer.backgroundColor = CGColorCreateGenericGray(0.5, 1.0);
        previewLayer.shadowRadius = 5.0;
        previewLayer.shadowOffset = CGSizeMake(3, -3);
        previewLayer.shadowColor = CGColorCreateGenericGray(0.0, 1.0);
        previewLayer.shadowOpacity = 0.6;
        [[cardPreviewView layer] addSublayer:previewLayer];
    }
    return self;
}

/**
 * Performs the operation by using the callback given to us by the game engine.
 *
 * @param simulate If YES, operation is simulated without actually changing state of the game.
 * @return YES if the callback operation was allowed, otherwise NO.
 */

- (BOOL)performCallback:(BOOL)simulate
{
    // Check if the callback has been set
    if (!callback)
        return NO;

    // Find all selected cards
    NSArray *selectedCards = [[choiceArrayController arrangedObjects] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"selected == YES"]];

    // Create an array containing the chosen cards in format accepted by the engine
    int numChosen = 0;
    design *chosen[DECK_SIZE];
    for (NSDictionary *dict in selectedCards)
        chosen[numChosen++] = [[dict valueForKey:@"design"] pointerValue];

    // Get pointer to the game engine state
    __strong game *pGame = theGame;

    if (simulate)
    {
        // Simulation was requested, make a copy of the game engine state and
        // specify simulation mode.
        pGame = NSAllocateCollectable(sizeof(game), 0);
        *pGame = *theGame;
        pGame->simulation = 1;
    }
    
    // Call back to the engine
    int result = callback(pGame, who, chosen, numChosen, dataPointer);

    // Consider a user choice to be a random event
    pGame->random_event = 1;

    if (!simulate)
    {
        // This was not a simulation so let's update the user interface
        NSAssert(result > 0, @"[BMCardSelectionSheet performCallback:] callback failed");
        [[[NSApp mainWindow] delegate] updateAll];
    }
    return result > 0;
}

/// Called when user chooses a card from the table view. This is used to update
/// the card preview.
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
    NSArray *objects = [choiceArrayController selectedObjects];
    if ([objects count] > 0)
    {
        design *d_ptr = [[[objects objectAtIndex:0] valueForKey:@"design"] pointerValue];
        previewLayer.contents = (id)[[BMCardImageStore imageStore] imageForDesign:d_ptr];
    }
}

@end
