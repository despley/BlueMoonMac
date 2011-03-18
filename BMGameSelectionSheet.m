/**
 * @file
 * Implementation of class BMGameSelectionSheet.
 */

#import "BMGameSelectionSheet.h"

@interface BMGameSelectionSheet ()
@property(readonly) BOOL selectionsAreValid;
@end

@implementation BMGameSelectionSheet

@synthesize sheet, randomSeed, computerDeck, humanDeck, startPlayer;

+ (void)initialize
{
    [self setKeys:[NSArray arrayWithObjects:@"computerDeck", @"humanDeck", nil] triggerChangeNotificationsForDependentKey:@"selectionsAreValid"];
}

+ (BMGameSelectionSheet *)sheet
{   
    return [[[self class] alloc] init];
}

- (id)init
{
    if ((self = [super init]))
    {
        startPlayer = 1;
        if (![NSBundle loadNibNamed:@"GameSelectionSheet" owner:self] || !sheet)
        {
            NSAssert(NO, @"[BMGameScelectionSheet init] Failed to load \"GameSelectionSheet.nib\"");
            return nil;
        }
    }
    return self;
}

- (IBAction)closeButtonWasClicked:(id)sender
{
    [sheet orderOut:sender];
    [NSApp endSheet:sheet returnCode:[sender tag]];
}

- (BOOL)selectionsAreValid
{
    return computerDeck != humanDeck;
}

@end
