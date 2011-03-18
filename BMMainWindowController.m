/**
 * @file
 * Implementation of class BMMainWindowController.
 */

#import "BMMainWindowController.h"
#import "BMCardSelectionSheet.h"
#import "BMGameSelectionSheet.h"
#import "BMCardImageStore.h"
#import "BMPreferencesWindowController.h"

const CGFloat marginX = 7;      ///< Margin at left and right sides of the game area
const CGFloat marginY = 10;     ///< Margin at top and bottom of the game area
const int spacing = 8;          ///< Spacing between cards in hand
const int stagger = 15;         ///< Spacing between cards in combat/support piles
const int windowWidth = 900;    ///< Width of the main game window
const int windowHeight = 780;   ///< Height of the main game window

const int TagDiscloseHand = 10; ///< Tag of the "Disclose Opponent's Hand" menu item

/// AI verbosity.
int verbose;

// Game engine calls this method when it wants to add text to the message view.
// Here we simply forward the request to BMMainWindowController.
void message_add(char *msg)
{
    [[[NSApp mainWindow] delegate] addMessage:[NSString stringWithUTF8String:msg]];
}

// Game engine calls this method when it needs a choice of cards from the user,
// e.g., for discarding, drawing cards or some other effect.
static void gui_choose(game *g, int chooser, int who, design **choices,
                       int num_choices, int min, int max,
                       choose_result callback, void *data, char *prompt)
{

	// Check for no real choice
	if (min == num_choices)
	{
		// Call callback
		callback(g, who, choices, num_choices, data);

		// Done
		return;
	}

    // Create a sheet for selecting the cards and forward all information 
    // we received from the engine to the sheet's controller. Then display
    // the sheet.
    BMCardSelectionSheet *sheet = [BMCardSelectionSheet sheet];
    [sheet setChoices:choices amount:num_choices];
    sheet.who = who;
    sheet.minAmount = min;
    sheet.maxAmount = max;
    sheet.callback = callback;
    sheet.dataPointer = data;
    sheet.theGame = g;
    sheet.prompt = [NSString stringWithUTF8String:prompt];
    [sheet display];    
}

// Interface for the engine so it can access the UI when needed.
static interface gui_func = { NULL, NULL, gui_choose, NULL, NULL };

/// Private methods of BMMainWindowController.
@interface BMMainWindowController ()
- (void)awakeFromNib;
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification;
- (void)saveState;
- (void)setPeople;
- (void)updateStatus;
- (void)updateTable;
- (void)updateHand:(BOOL)playerHand;
- (CALayer *)createCardLayer:(design *)d_ptr point:(CGPoint)point action:(SEL)cardAction visibleWidth:(int)visibleWidth;
- (void)updateCardLayer:(CALayer *)layer design:(design *)d_ptr action:(SEL)cardAction;
- (CALayer *)createPileLayerWithSize:(int)size atPoint:(CGPoint)point;
- (CALayer *)createBadgeLayer:(CALayer *)stackLayer count:(int)count;
@end

@implementation BMMainWindowController

@synthesize statusText;

- (BOOL)gameStarted
{
    return [buttonTabView indexOfTabViewItem:[buttonTabView selectedTabViewItem]] == 1;
}

/// Handles a request to start a new game with Game->New menu selection.
- (IBAction)onNewGame:(id)sender
{
    // Clear output view
    [messageView setString:@""];

    for (CALayer *layer in playerCardLayers)
        layer.hidden = YES;

    for (CALayer *layer in aiCardLayers)
        layer.hidden = YES;
    
    [buttonTabView selectTabViewItemAtIndex:0];
    [self setPeople];
    [self updateTable];
}

// Handles press of the "Retreat" button. Retreats from the fight and 
// lets the AI take a turn.
- (IBAction)retreatButtonWasClicked:(id)sender
{
    // If user needs to discard cards, we must end the turn after the discard.
    // Discard sheet will end the turn after it is done.
    [BMCardSelectionSheet sheet].endTurnAfter = YES;

    // Tell the engine to retreat. This may subsequently call gui_choose which
    // will display BMCardSelectionSheet.
    retreat(&real_game);

    if (real_game.p[player_us].phase == PHASE_START)
    {
        // There was no need to discard, so we end the turn here.
        [BMCardSelectionSheet sheet].endTurnAfter = NO;
        [self handleEndTurn];
    }
}

//Handles press of the "Announce" button.
- (IBAction)announceButtonWasClicked:(id)sender
{
    int old_turn = real_game.turn;
	
    // Handle end of support phase
	end_support(&real_game);

    // Announce power
    int element = [sender tag];
    announce_power(&real_game, element);

	// Refresh hand
	refresh_phase(&real_game);

	// End turn
	end_turn(&real_game);

	// Check for change of turn due to forced retreat
	if (real_game.turn != old_turn)
	{
		// Let AI take turn
		[self handleEndTurn];
		return;
	}
    
	// Turn over 
	real_game.p[player_us].phase = PHASE_NONE;

	// Go to next player
	real_game.turn = !real_game.turn;

	// Start next player's turn
	real_game.p[real_game.turn].phase = PHASE_START;

	// Let AI take turn 
    [self handleEndTurn];
}

// Handles press of the "Undo" button.
- (IBAction)undoButtonWasClicked:(id)sender
{
    NSAssert(backup_set, @"[BMMainWindowController undoButtonWasClicked:] Undo was clicked but there's no backup!");

    // Restore game 
	real_game = backup;

	// No backup available 
	backup_set = NO;

	// Deactivate undo button
    [undoButton setEnabled:NO];

	// Retreat is always legal after undo
    [retreatButton setEnabled:YES];

	// Announce power is always illegal
    [fireButton setEnabled:NO];
    [earthButton setEnabled:NO];

	// Draw stuff
    [self updateAll];
    
    [self addMessage:NSLocalizedString(@"Current turn undone.\n", @"Message shown when user clicks undo button")];
}

// Handles a click on one of the cards.
- (IBAction)cardWasClicked:(CALayer *)layer
{
    // Get info dictionary from the layer
    NSDictionary *info = [layer valueForKey:@"info"];

    // Get card design
	design *d_ptr = [[info valueForKey:@"design"] pointerValue];
    if (!d_ptr)
        return;
    
	player *p = &real_game.p[player_us];

    [self saveState];
    
    // If alt is pressed, ignore effect of the card
    int ignoreEffect = ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask) != 0;
    
	// Play card
	play_card(&real_game, d_ptr, ignoreEffect, 0);

	// Check for leadership card
	if (d_ptr->type == TYPE_LEADERSHIP)
	{
		// Set phase to retreat
		p->phase = PHASE_RETREAT;
	}
	else if (d_ptr->type == TYPE_CHARACTER)
	{
		// Set phase to character
		p->phase = PHASE_CHAR;
	}
	else
	{
		// Set phase to booster/support
		p->phase = PHASE_SUPPORT;
	}

	// Update UI state
    [self updateButtons];
    [self updateAll];
}

/// Called when user clicks one of the start buttons.
- (IBAction)startButtonWasClicked:(id)sender
{
    // Set up people for the game
    [self setPeople];

    // Display random number seed of the game in output
    NSString *sSeed = NSLocalizedString(@"Random seed: %u\n", "Message showing seed value for random number generator");
    [self addMessage:[NSString stringWithFormat:sSeed, real_game.start_seed]];

    // Display in-game action buttons
    [buttonTabView selectTabViewItemAtIndex:1];

	// Have start player begin
    real_game.turn = [sender tag] ? player_us : !player_us;
	real_game.p[real_game.turn].phase = PHASE_START;
	real_game.p[!real_game.turn].phase = PHASE_NONE;

	// Let the AI player take their turn and setup for our first turn
	[self handleEndTurn];
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem
{
    if ([menuItem tag] == TagDiscloseHand && !self.gameStarted)
        return NO;
    return YES;
}

/// Discloses opponent's hand when the option is selected from Debug menu.
- (IBAction)debugDiscloseHand:(id)sender
{
    if (!self.gameStarted)
        return;
        
    // Get opponent player pointer
    player *p = &real_game.p[!player_us];
        
    // Loop over cards
    for (int i = 1; i < DECK_SIZE; i++)
    {  
        // Get card pointer
        card *c = &p->deck[i];
        
        // Disclose card if it is in hand
        if (c->where == LOC_HAND)
            c->disclosed = 1;
    }    
    [self updateHand:NO];
}

- (IBAction)debugSelectGame:(id)sender
{
    BMGameSelectionSheet *sheet = [BMGameSelectionSheet sheet];
    sheet.randomSeed = real_game.start_seed;
    sheet.humanDeck = human_people;
    sheet.computerDeck = ai_people;
    [NSApp beginSheet:[sheet valueForKey:@"sheet"]
       modalForWindow:[self window]
        modalDelegate:self
       didEndSelector:@selector(selectGameSheetDidEnd:returnCode:contextInfo:)
          contextInfo:NULL];
}

- (IBAction)showPreferencesWindow:(id)sender
{
    BMPreferencesWindowController *controller = [[BMPreferencesWindowController alloc] initWithWindowNibName:@"Preferences"];
    [[controller window] center];
    [controller showWindow:sender];
}

- (void)selectGameSheetDidEnd:(NSWindow *)sheet returnCode:(int)returnCode contextInfo:(void *)contextInfo
{
    BMGameSelectionSheet *s = [sheet delegate];
    if (returnCode == NSOKButton)
    {
        real_game.random_seed = s.randomSeed;
        human_people = s.humanDeck;
        ai_people = s.computerDeck;

        // Set up people for the game
        [self setPeople];

        // Clear output view and isplay random number seed of the game in output   
        NSString *sSeed = NSLocalizedString(@"Random seed: %u\n", "Message showing seed value for random number generator");
        [messageView setString:[NSString stringWithFormat:sSeed, real_game.start_seed]];

        // Display in-game action buttons
        [buttonTabView selectTabViewItemAtIndex:1];

        // Have start player begin
        real_game.turn = s.startPlayer ? player_us : !player_us;
        real_game.p[real_game.turn].phase = PHASE_START;
        real_game.p[!real_game.turn].phase = PHASE_NONE;

        // Let the AI player take their turn and setup for our first turn
        [self handleEndTurn];
    }
}

/// Adds a message to the message area.
- (void)addMessage:(NSString *)message
{
    NSTextStorage *storage = [messageView textStorage];
    [storage replaceCharactersInRange:NSMakeRange([storage length], 0) withString:message];
    [messageView scrollRangeToVisible:NSMakeRange([storage length], 0)];
}

#pragma mark Internal methods

/**
 * Saves current game state as "undo" state.
 */
- (void)saveState
{
    if (!backup_set)
    {
        backup = real_game;
        backup_set = YES;
        [undoButton setEnabled:YES];
    }
}

- (void)awakeFromNib
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults registerDefaults:[NSDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithInt:0], @"aiPeople",
        [NSNumber numberWithInt:1], @"playerPeople",
        nil]];

    imageStore = [BMCardImageStore imageStore];
    
    const int numCards = 6;
    // Load card designs
    NSString *cardFilePath = [[NSBundle mainBundle] pathForResource:@"cards" ofType:@"txt"];
    [[NSFileManager defaultManager] changeCurrentDirectoryPath:[cardFilePath stringByDeletingLastPathComponent]];
    read_cards();
    
    ai_people = [defaults integerForKey:@"aiPeople"];
    human_people = [defaults integerForKey:@"playerPeople"];

    [[self window] setFrame:NSMakeRect(0, 0, windowWidth, windowHeight) display:YES];
    [[self window] center];

    tableLayers = [NSMutableArray array];

    cardWidth = ([gameView frame].size.width - marginX * 2 - spacing * (numCards - 1)) / numCards;
    cardHeight = cardWidth * imageStore.cardSize.height / imageStore.cardSize.width;
    
    gameLayer = [CALayer layer];
    [gameView setLayer:gameLayer];
    [gameView setWantsLayer:YES];
//    gameLayer.actions = [NSDictionary dictionaryWithObject:[NSNull null] forKey:@"sublayers"];
        
    CGFloat yPos = marginY + cardHeight + spacing * 2;
    
    // Create layer for player's support pile
    playerSupportLayer = [self createPileLayerWithSize:3 atPoint:CGPointMake(marginX, yPos)];
    [playerSupportLayer setNeedsDisplay];

    // Create layer for player's combat pile
    playerCombatLayer = [self createPileLayerWithSize:4 atPoint:CGPointMake(marginX + cardWidth + spacing + 40, yPos)];
    [playerCombatLayer setNeedsDisplay];

    // Create layer for player's deck pile
    CALayer *layer = [self createPileLayerWithSize:1 atPoint:CGPointMake(marginX + 3 * (cardWidth + spacing), yPos)];
    [layer setNeedsDisplay];
    
    // Create layer for player's discard pile
    playerDiscardLayer = [self createPileLayerWithSize:1 atPoint:CGPointMake(marginX + 4 * (cardWidth + spacing), yPos)];
    [playerDiscardLayer setNeedsDisplay];

    layerPlayerDiscard = [self createCardLayer:NULL
                                         point:CGPointMake(marginX + 4 * (cardWidth + spacing), yPos)
                                        action:nil
                                  visibleWidth:0];
    layerPlayerDiscard.hidden = YES;

    yPos = marginY + cardHeight * 2.1 + spacing * 2;

    // Create layer for AI's support pile
    aiSupportLayer = [self createPileLayerWithSize:3 atPoint:CGPointMake(marginX, yPos)];
    [aiSupportLayer setNeedsDisplay];

    // Create layer for AI's combat pile
    aiCombatLayer = [self createPileLayerWithSize:4 atPoint:CGPointMake(marginX + cardWidth + spacing + 40, yPos)];
    [aiCombatLayer setNeedsDisplay];

    // Create layer for AI's deck pile
    layer = [self createPileLayerWithSize:1 atPoint:CGPointMake(marginX + 3 * (cardWidth + spacing), yPos)];
    [layer setNeedsDisplay];

    // Create layer for AI's discard pile
    aiDiscardLayer = [self createPileLayerWithSize:1 atPoint:CGPointMake(marginX + 4 * (cardWidth + spacing), yPos)];
    [aiDiscardLayer setNeedsDisplay];
    
    layerAiDiscard = [self createCardLayer:NULL
                                     point:CGPointMake(marginX + 4 * (cardWidth + spacing), yPos)
                                    action:nil
                              visibleWidth:0];
    layerAiDiscard.hidden = YES;    

    [previewView setLayer:[CALayer layer]];
    [previewView setWantsLayer:YES];

    CALayer *previewBack = [CALayer layer];
    previewBack.frame = CGRectMake(9, 8, 230, 409);
    previewBack.borderWidth = 3.0;
    previewBack.borderColor = CGColorCreateGenericRGB(0.5, 0.5, 0.5, 0.5);
    previewBack.backgroundColor = CGColorCreateGenericRGB(0.7, 0.7, 0.7, 0.1);
    previewBack.cornerRadius = 10.0;
    [[previewView layer] addSublayer:previewBack];

    previewLayer = [CALayer layer];
    previewLayer.frame = previewBack.frame;
    previewLayer.hidden = YES;
    previewLayer.shadowRadius = 3.0;
    previewLayer.shadowOffset = CGSizeMake(3, -3);
    previewLayer.shadowColor = CGColorCreateGenericGray(0.0, 1.0);
    previewLayer.shadowOpacity = 0.6;
    [[previewView layer] addSublayer:previewLayer];

    // Create layers for decks
    CGFloat xPos = marginX + 3 * (cardWidth + spacing);
    deckPlayer = [self createCardLayer:NULL 
                                 point:CGPointMake(xPos, marginY + cardHeight + spacing * 2) 
                                action:nil 
                          visibleWidth:0];

    deckAI = [self createCardLayer:NULL 
                             point:CGPointMake(xPos, marginY + cardHeight * 2.1 + spacing * 2) 
                            action:nil 
                      visibleWidth:0];

}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    playerCardLayers = [NSMutableArray arrayWithCapacity:6];
    aiCardLayers = [NSMutableArray arrayWithCapacity:6];
                          
    // Set random seed
    srand(time(NULL));
    real_game.random_seed = time(NULL);
      
    // Set people pointers 
    [self setPeople];

    [self updateAll];
}

- (void)windowWillClose:(NSNotification *)notification
{
    [NSApp terminate:nil];
}

- (void)applicationWillTerminate:(NSNotification *)aNotification
{
    [[NSUserDefaults standardUserDefaults] setInteger:human_people forKey:@"playerPeople"];
    [[NSUserDefaults standardUserDefaults] setInteger:ai_people forKey:@"aiPeople"];
}

- (void)setPeople
{
	// Check for human player having later people 
	if (human_people > ai_people)
	{
		// Set player peoples
		real_game.p[0].p_ptr = &peoples[ai_people];
		real_game.p[1].p_ptr = &peoples[human_people];

		// Human is second player
		player_us = 1;
	}
	else
	{
		// Set player peoples 
		real_game.p[0].p_ptr = &peoples[human_people];
		real_game.p[1].p_ptr = &peoples[ai_people];

		// Human is first player 
		player_us = 0;
	}

	// Initialize game 
	init_game(&real_game, 1);

	// Set opponent interface 
	real_game.p[!player_us].control = &ai_func;

	// Call opponent initialization
	real_game.p[!player_us].control->init(&real_game, !player_us);

	// Set our interface
	real_game.p[player_us].control = &gui_func;
}

/**
 * After our turn, let the AI take actions until it is our turn again,
 * then refresh the drawing areas so that we can take our next turn.
 */
- (void)handleEndTurn
{
	// Deactivate "retreat" button
    [retreatButton setEnabled:NO];

	// Deactivate "undo" button
	[undoButton setEnabled:NO];

	// Deactivate "announce" buttons 
    [fireButton setEnabled:NO];
    [earthButton setEnabled:NO];

	// Have AI take actions until it is our turn 
	while (real_game.turn != player_us && !real_game.game_over)
	{
		// Request action from AI
		real_game.p[real_game.turn].control->take_action(&real_game);

		// Redraw everything 
		//[self updateAll];        
	}

	// No undo available
	backup_set = NO;

	// Set "retreat" button
    [retreatButton setEnabled:!real_game.game_over];

	// Start turn
	start_turn(&real_game);

	// Advance to beginning of turn phase
	real_game.p[player_us].phase = PHASE_BEGIN;

	// Draw stuff 
    [self updateAll];
}

/// Updates control buttons according to state of the game
- (void)updateButtons
{
	player *p = &real_game.p[player_us];
	BOOL fire, earth;

	// Check for retreat legal
	BOOL retreat = p->phase <= PHASE_RETREAT;

	// Check for incomplete turn
	if (!p->char_played)
	{
		// Cannot announce yet
		fire = earth = NO;
	}

	// Check for no fight started
	else if (!real_game.fight_started)
	{
		// Both are legal
		fire = earth = YES;
	}

	// Check for enough power
	else if (compute_power(&real_game, player_us) >=
		 compute_power(&real_game, !player_us))
	{
		// Check for fire element
		fire = !real_game.fight_element;
		earth = real_game.fight_element;
	}

	// Insufficent power, check for shields 
	else
	{
		// Assume neither are legal 
		fire = earth = NO;

		// Look for active shield
		for (int i = 0; i < DECK_SIZE; i++)
		{
			// Get card pointer
			card *c = &p->deck[i];

			// Skip inactive cards
			if (!c->active) continue;

			// Check for shield
			if (c->icons & (1 << real_game.fight_element))
			{
				// Shields make announcement ok
				fire = !real_game.fight_element;
				earth = real_game.fight_element;
			}
		}
	}

	// Check for unsatisfied opponent cards
	if (fire || earth)
	{
		// Simulate game
		game sim = real_game;

		// Set simulation flag
		sim.simulation = 1;

		// Set control interface to AI
		sim.p[player_us].control = &ai_func;

		// Check for illegal end turn 
		if (!check_end_support(&sim)) fire = earth = NO;
	}

	// Activate/deactivate buttons 
    [retreatButton setEnabled:retreat];
    [fireButton setEnabled:fire];
    [earthButton setEnabled:earth];
}

/**
 * Creates a new layer for the specified card. The card is automatically added
 * to gameLayer.
 *
 * @param d_ptr         Pointer to card design (NULL means that the card is face down)
 * @param point         Location of the bottom left corner of the layer
 * @param cardAction    Selector that will be called when the card is clicked (nil=no action)   
 * @param visibleWidth  If card is partially hidden, this tells amount of pixels visible (0=completely visible)
 *
 * @return Pointer to newly created layer.
 */
- (CALayer *)createCardLayer:(design *)d_ptr 
                       point:(CGPoint)point
                      action:(SEL)cardAction
                visibleWidth:(int)visibleWidth
{
    // Create layer for the card 
    CALayer *layer = [CALayer layer];
    layer.anchorPoint = CGPointMake(0.5, 0.5);
    layer.frame = CGRectMake(point.x, point.y, cardWidth, cardHeight);

    // Set up shadow for the layer
    layer.shadowRadius = 5.0;
    layer.shadowOffset = CGSizeMake(3, -3);
    layer.shadowColor = CGColorCreateGenericGray(0.0, 1.0);
    layer.shadowOpacity = 0.6;
    
    // Set up image of the card
    layer.contents = (id)[imageStore imageForDesign:d_ptr];
        
    // Add card layer to gameLayer
    [gameLayer addSublayer:layer];

    // Create info dictionary for the card
    NSMutableDictionary *infoDict = [NSMutableDictionary dictionaryWithObject:layer forKey:@"layer"];

    // Store card's action to info dictionary
    if (cardAction)
        [infoDict setValue:NSStringFromSelector(cardAction) forKey:@"action"];

    // Store card's design to info dictionary
    if (d_ptr)
        [infoDict setValue:[NSValue valueWithPointer:d_ptr] forKey:@"design"];

    // Store info dictionary to the layer
    [layer setValue:[NSDictionary dictionaryWithDictionary:infoDict] forKey:@"info"];

    if (visibleWidth == 0)
        visibleWidth = cardWidth;

    // Create a tracking area so we get mouse notifications for the card
    NSTrackingArea *area = [[NSTrackingArea alloc] initWithRect:NSMakeRect(point.x, point.y, visibleWidth, cardHeight)
                                                        options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow
                                                          owner:self
                                                       userInfo:[NSDictionary dictionaryWithObject:layer forKey:@"layer"]];

    // Add the tracking area to gameView
    [gameView addTrackingArea:area];
    
    // Store the tracking area to the layer
    [layer setValue:area forKey:@"trackingArea"];

    return layer;
}

/**
 * Updates a card layer by changing it's card design and action.
 *
 * @param layer         Card layer to be updated
 * @param d_ptr         Pointer to card design (NULL means that the card is face down)
 * @param cardAction    Selector that will be called when the card is clicked (nil=no action)   
 */
- (void)updateCardLayer:(CALayer *)layer design:(design *)d_ptr action:(SEL)cardAction
{
    // Set up image of the card
    layer.contents = (id)[imageStore imageForDesign:d_ptr];

    // Create info dictionary for the card
    NSDictionary *infoDict = [NSMutableDictionary dictionaryWithObject:layer forKey:@"layer"];

    // Store card's action to info dictionary
    if (cardAction)
        [infoDict setValue:NSStringFromSelector(cardAction) forKey:@"action"];

    // Store card's design to info dictionary
    if (d_ptr)
        [infoDict setValue:[NSValue valueWithPointer:d_ptr] forKey:@"design"];

    // Store info dictionary to the layer
    [layer setValue:infoDict forKey:@"info"];
}

/**
 * Updates a card layer by changing it's card design and action.
 *
 * @param layer         Card layer to be updated
 * @param d_ptr         Pointer to card design (NULL means that the card is face down)
 * @param cardAction    Selector that will be called when the card is clicked (nil=no action)   
 * @param visibleWidth  If card is partially hidden, this tells amount of pixels visible (0=completely visible)
 */
- (void)updateCardLayer:(CALayer *)layer design:(design *)d_ptr action:(SEL)cardAction visibleWidth:(int)visibleWidth
{
    [self updateCardLayer:layer design:d_ptr action:cardAction];

    CGPoint point = layer.frame.origin;
    
    if (visibleWidth == 0)
        visibleWidth = cardWidth;

    // Create a tracking area so we get mouse notifications for the card
    NSTrackingArea *area = [[NSTrackingArea alloc] initWithRect:NSMakeRect(point.x, point.y, visibleWidth, cardHeight)
                                                        options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow
                                                          owner:self
                                                       userInfo:[NSDictionary dictionaryWithObject:layer forKey:@"layer"]];

    // Store the tracking area to the layer
    NSTrackingArea *previousArea = [layer valueForKey:@"trackingArea"];
    [layer setValue:area forKey:@"trackingArea"];

    // Replace previous tracking area in gameView
    if (previousArea)    
        [gameView removeTrackingArea:previousArea];
    [gameView addTrackingArea:area];
}

/// Creates a layer that displays background of a pile in the game area.
- (CALayer *)createPileLayerWithSize:(int)size atPoint:(CGPoint)point
{
    CGFloat wi = cardWidth + stagger * (size - 1);
    CALayer *layer = [CALayer layer];
    layer.frame = CGRectMake(point.x, point.y, wi, cardHeight);
    layer.delegate = self;
    [gameLayer addSublayer:layer];
    return layer;
}

/// Updates the entire game area
- (void)updateAll
{
    [CATransaction begin];
    [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
    [self updateStatus];
    [self updateTable];
    [self updateHand:NO];
    [self updateHand:YES];
    [CATransaction commit];
}

/// Updates status text at bottom of the window
- (void)updateStatus
{
	// Get player pointers
	player *p1 = &real_game.p[player_us];
	player *p2 = &real_game.p[!player_us];

    const NSString *amountStrings[] = { 
        NSLocalizedString(@"one dragon", @"String describing dragon count"), 
        NSLocalizedString(@"two dragons", @"String describing dragon count"),
        NSLocalizedString(@"three dragons", @"String describing dragon count")
    };

    int amount;
    const char *name = NULL;
    if (p1->dragons > 0)
    {
        name = p1->p_ptr->name;
        amount = p1->dragons;
    }
    else if (p2->dragons > 0)
    {
        name = p2->p_ptr->name;
        amount = p2->dragons;
    }

    if (amount > 3)
        amount = 3;

    if (name)
    {
        NSString *sFormat = NSLocalizedString(@"%s has attracted %@.", @"Status message showing how many dragons a player has attracted");
        self.statusText = [NSString stringWithFormat:sFormat, name, amountStrings[amount-1]];
    }
    else
        self.statusText = NSLocalizedString(@"Dragons are neutral.", @"Status message showing that the game is even (no dragons on either side");

}

/// Updates all cards in the game area.
- (void)updateTable
{
    // Delete old layers
    for (CALayer *layer in tableLayers)
    {
        [gameView removeTrackingArea:[layer valueForKey:@"trackingArea"]];
        [layer removeFromSuperlayer];
    }
    [tableLayers removeAllObjects];
    
	player *p = &real_game.p[player_us];
    player *opp = &real_game.p[!player_us];
	card *list[DECK_SIZE];
	BOOL retrieve_char = YES;

	// Loop over opponent cards
	for (int i = 1; i < DECK_SIZE; i++)
	{
		// Get card pointer
		card *c = &opp->deck[i];

		// Skip inactive cards
		if (!c->active) continue;

		// Skip non-character cards
		if (c->d_ptr->type != TYPE_CHARACTER) continue;

		// Check for depicted RETRIEVE icon
		if (c->d_ptr->icons & ICON_RETRIEVE)
		{
			// Retrieving our own characters is disallowed
			retrieve_char = NO;
		}
	}

    int xPos = marginX;
    int yPos =  marginY + cardHeight + spacing * 2;

	// Count support cards
	int num_support = p->stack[LOC_SUPPORT];

	// Check for support cards played
	if (num_support)
	{
		// No cards drawn yet
		int cardIndex = 0;

		// Loop over cards
		for (int i = 0; i < DECK_SIZE; i++)
		{
			// Get card pointer
			card *c = &p->deck[i];

            SEL cardAction = nil;
            
			// Skip cards not in support pile
			if (c->where != LOC_SUPPORT) continue;

			// Check for retrievable
			if (real_game.turn == player_us &&
			    p->phase == PHASE_BEGIN &&
			    (c->icons & ICON_RETRIEVE) &&
			    p->stack[LOC_HAND] < hand_limit(&real_game, player_us))
			{
                cardAction = @selector(retrieveCard:);
			}

			// Check for "on my turn" special power
			if (real_game.turn == player_us &&
			    c->d_ptr->special_time == TIME_MYTURN &&
			    !c->used && !c->text_ignored)
			{
                cardAction = @selector(cardWasUsed:);
			}

            // Create a layer for the card            
            CALayer *layer = [self createCardLayer:c->d_ptr 
                                             point:CGPointMake(xPos + cardIndex * stagger, yPos) 
                                            action:cardAction
                                      visibleWidth:(cardIndex < num_support - 1) ? stagger : 0];
            [tableLayers addObject:layer];

			// One more support card drawn
			cardIndex++;
		}
	}

    // Create badge if player has more than one support card
    [self createBadgeLayer:playerSupportLayer count:p->stack[LOC_SUPPORT]];

	// Assume no active combat cards
	int num_combat = 0;

	// Loop over our cards
	for (int i = 1; i < DECK_SIZE; i++)
	{
		// Get card pointer
		card *c = &p->deck[i];

		// Skip cards not in combat area
		if (c->where != LOC_COMBAT) continue;

		// Skip non-character cards
		if (c->d_ptr->type != TYPE_CHARACTER) continue;

		// Skip inactive cards
		if (!c->active) continue;

		// Add card to list
		list[num_combat++] = c;
	}

	// Loop over our cards
	for (int i = 1; i < DECK_SIZE; i++)
	{
		// Get card pointer
		card *c = &p->deck[i];

		// Skip cards not in combat area
		if (c->where != LOC_COMBAT) continue;

		// Skip non-booster cards
		if (c->d_ptr->type != TYPE_BOOSTER) continue;

		// Skip inactive cards
		if (!c->active) continue;

		// Add card to list
		list[num_combat++] = c;
	}

	// Check for combat cards to draw
	if (num_combat)
	{
		// No cards drawn yet
		int cardIndex = 0;

        // Determine location of first combat card
        int xPos = marginX + cardWidth + spacing + 40;

		// Loop over cards in list
		for (int i = 0; i < num_combat; i++)
		{
			// Get card pointer
			card *c = list[i];

            SEL cardAction = nil;

			// Check for retrievable
			if (real_game.turn == player_us &&
			    p->phase == PHASE_BEGIN &&
			    (c->icons & ICON_RETRIEVE) &&
			    (retrieve_char || c->d_ptr->type == TYPE_BOOSTER) &&
			    p->stack[LOC_HAND] <
			        hand_limit(&real_game, player_us))
			{
                cardAction = @selector(retrieveCard:);
			}

			// Check for "on my turn" special power
			if (real_game.turn == player_us &&
			    c->d_ptr->special_time == TIME_MYTURN &&
			    !c->used && !c->text_ignored)
			{
                cardAction = @selector(cardWasUsed:);
			}
            
            // Create a layer for the card            
            CALayer *layer = [self createCardLayer:c->d_ptr 
                                             point:CGPointMake(xPos + cardIndex * stagger, yPos)
                                            action:cardAction
                                      visibleWidth:(cardIndex < num_combat - 1) ? stagger : 0];
            [tableLayers addObject:layer];

			// One more combat card drawn
			cardIndex++;
		}
	}

	// Check for cards in draw pile
    deckPlayer.hidden = p->stack[LOC_DRAW] == 0;

    // Create badge if player has more than one card in combat pile
    [self createBadgeLayer:playerCombatLayer count:p->stack[LOC_COMBAT]];

    // Create badge if player has more than one card in discard pile
    [self createBadgeLayer:playerDiscardLayer count:p->stack[LOC_DISCARD]];    

    // Create badge if player has more than one card in draw pile
    [self createBadgeLayer:deckPlayer count:self.gameStarted ? p->stack[LOC_DRAW] : 30];

	// Check for discard
    if (p->last_discard)
	{
        [self updateCardLayer:layerPlayerDiscard design:p->last_discard action:nil];
        layerPlayerDiscard.hidden = NO;
    }
    else
    {
        layerPlayerDiscard.hidden = YES;
    }

	// Create or update leadership card for human player
    SEL cardAction = nil;
    if ([buttonTabView indexOfTabViewItem:[buttonTabView selectedTabViewItem]] == 0)
        cardAction = @selector(changePeople:);

    design *d_ptr = self.gameStarted ? p->last_leader : &peoples[human_people].deck[0];
    if (!layerPlayerLeader)
    {
        layerPlayerLeader = [self createCardLayer:d_ptr
                                            point:CGPointMake(marginX + 5 * (cardWidth + spacing), yPos)
                                        action:cardAction
                                    visibleWidth:0];
    }
    else
    {
        [self updateCardLayer:layerPlayerLeader design:d_ptr action:cardAction];
    }
    
	// Clear number of combat and support cards drawn
	num_combat = 0;

	// Count support cards
	num_support = opp->stack[LOC_SUPPORT];

    yPos = marginY + cardHeight * 2.1 + spacing * 2;

	// Check for support cards played
	if (num_support)
	{    
        int xPos = marginX;
 
        int cardIndex = 0;

		// Loop over cards
		for (int i = 0; i < DECK_SIZE; i++)
		{
			// Get card pointer
			card *c = &opp->deck[i];

            SEL cardAction = nil;

			// Skip cards not in support pile
			if (c->where != LOC_SUPPORT) continue;

			// Check for card to satisfy
			if (real_game.turn == player_us &&
			    c->d_ptr->special_cat == 7 &&
			    (c->d_ptr->special_effect & S7_DISCARD_MASK) &&
			    !c->text_ignored && !c->used &&
			    satisfy_possible(&real_game, c->d_ptr))
			{
                cardAction = @selector(cardWasSatisfied:);
			}

            // Create a layer for the card            
            CALayer *layer = [self createCardLayer:c->d_ptr 
                                            point:CGPointMake(xPos + cardIndex * stagger, yPos)
                                            action:cardAction
                                      visibleWidth:(cardIndex < num_support - 1) ? stagger : 0];
            [tableLayers addObject:layer];

			// One more support card drawn
			cardIndex++;
		}
	}

    // Create badge if AI has more than one support card
    [self createBadgeLayer:aiSupportLayer count:opp->stack[LOC_SUPPORT]];

	// Assume no active combat cards
	num_combat = 0;

	// Loop over opponent cards
	for (int i = 0; i < DECK_SIZE; i++)
	{
		// Get card pointer
		card *c = &opp->deck[i];

		// Skip cards not in combat area
		if (c->where != LOC_COMBAT) continue;

		// Skip non-character cards
		if (c->d_ptr->type != TYPE_CHARACTER) continue;

		// Skip inactive cards
		if (!c->active) continue;

		// Add card to list
		list[num_combat++] = c;
	}

	// Loop over opponent cards
	for (int i = 0; i < DECK_SIZE; i++)
	{
		// Get card pointer
		card *c = &opp->deck[i];

		// Skip cards not in combat area
		if (c->where != LOC_COMBAT) continue;

		// Skip non-booster cards
		if (c->d_ptr->type != TYPE_BOOSTER) continue;

		// Skip inactive cards
		if (!c->active) continue;

		// Add card to list
		list[num_combat++] = c;
	}

	// Check for combat cards to draw
	if (num_combat)
	{
		// No cards drawn yet
		int cardIndex = 0;

        // Determine location of first combat card
        int xPos = marginX + cardWidth + spacing + 40;
        
		// Loop over cards
		for (int i = 0; i < num_combat; i++)
		{
			// Get card pointer
			card *c = list[i];

            SEL cardAction = nil;

			if (real_game.turn == player_us &&
			    c->d_ptr->special_cat == 7 &&
			    (c->d_ptr->special_effect & S7_DISCARD_MASK) &&
			    !c->text_ignored && !c->used &&
			    satisfy_possible(&real_game, c->d_ptr))
			{
                cardAction = @selector(cardWasSatisfied:);
			}

            // Create a layer for the card            
            CALayer *layer = [self createCardLayer:c->d_ptr 
                                             point:CGPointMake(xPos + cardIndex * stagger, yPos)
                                            action:cardAction
                                      visibleWidth:(cardIndex < num_combat - 1) ? stagger : 0];
            [tableLayers addObject:layer];

			// One more support card drawn
			cardIndex++;
		}
	}

	// Check for cards in draw pile
    deckAI.hidden = p->stack[LOC_DRAW] == 0;

    // Create badge if player has more than one card in combat pile
    [self createBadgeLayer:aiCombatLayer count:opp->stack[LOC_COMBAT]];

    // Create badge if player has more than one card in discard pile
    [self createBadgeLayer:aiDiscardLayer count:opp->stack[LOC_DISCARD]];    

    // Create badge if player has more than one card in draw pile
    [self createBadgeLayer:deckAI count:self.gameStarted ? opp->stack[LOC_DRAW] : 30];

	// Check for discard
	if (opp->last_discard)
	{
        [self updateCardLayer:layerAiDiscard design:opp->last_discard action:nil];
        layerAiDiscard.hidden = NO;
    }
    else
    {
        layerAiDiscard.hidden = YES;
    }

	// Create or update leadership card for AI player
    cardAction = nil;
    if ([buttonTabView indexOfTabViewItem:[buttonTabView selectedTabViewItem]] == 0)
        cardAction = @selector(changePeople:);

    d_ptr = self.gameStarted ? opp->last_leader : &peoples[ai_people].deck[0];
    if (!layerAiLeader)
    {
        layerAiLeader = [self createCardLayer:d_ptr
                                        point:CGPointMake(marginX + 5 * (cardWidth + spacing), yPos)
                                       action:cardAction
                                 visibleWidth:0];
    }
    else
    {
        [self updateCardLayer:layerAiLeader design:d_ptr action:cardAction];
    }
}

- (void)updateHand:(BOOL)playerHand
{
    // Get player pointer
    player *p = &real_game.p[playerHand ? player_us : !player_us];

    // Number of cards in player's hand
    int numCards = p->stack[LOC_HAND];

	// Calculate positions for the hand cards
    CGFloat xStep = cardWidth + spacing;
    int visibleWidth = cardWidth;
	if (numCards > 6)
    {
        CGFloat first = marginX;
        CGFloat last = first + 5 * spacing + 5 * cardWidth;
        xStep = (last - first) / ((CGFloat)numCards - 1);    
        visibleWidth = xStep;
    }

    // Get array of layers for this player
    NSMutableArray *layerArray = playerHand ? playerCardLayers : aiCardLayers;

	// Clear count
	int cardIndex = 0;

    // Get position of first card for this player
    CGFloat xPos = marginX;  
    CGFloat yPos = playerHand ? marginY : marginY + cardHeight * 3.1 + spacing * 3;;

	// Loop over cards
    if (self.gameStarted)
    {
        for (int i = 0; i < DECK_SIZE; i++)
        {
            // Get card pointer
            card *c = &p->deck[i];

            // Get card design
            design *d_ptr = c->d_ptr;

            // Skip cards not in hand
            if (c->where != LOC_HAND) continue;

            // Clear design pointer if AI's card is not disclosed
            if (!playerHand && !c->disclosed) d_ptr = NULL;
            
            // Assume card can be played
            SEL cardAction = @selector(cardWasClicked:);
            
            // It is not possible to play from opponent's hand
            if (!playerHand)
                cardAction = nil;

            // Never legal to play cards after game is over
            else if (real_game.game_over) 
                cardAction = nil;

            // Not legal to play cards when not our turn
            else if (real_game.turn != player_us) 
                cardAction = nil;

            // Check for late leadership card
            else if (d_ptr->type == TYPE_LEADERSHIP && p->phase > PHASE_LEADER)
                cardAction = nil;

            // Check for late character cards
            else if (d_ptr->type == TYPE_CHARACTER && p->phase > PHASE_CHAR)
                cardAction = nil;

            // Check for early support/booster cards
            else if ((d_ptr->type == TYPE_SUPPORT || d_ptr->type == TYPE_BOOSTER) && p->char_played == 0)
                cardAction = nil;

            // Check for other restrictions on playing
            else if (!card_allowed(&real_game, d_ptr)) 
                cardAction = nil;

            CALayer *layer = nil;
            if (cardIndex < [layerArray count])
            {
                layer = [layerArray objectAtIndex:cardIndex];
                layer.frame = CGRectMake(xPos, yPos, cardWidth, cardHeight);
                [self updateCardLayer:layer design:d_ptr action:cardAction visibleWidth:visibleWidth];
                layer.hidden = NO;
            }
            else
            {
                layer = [self createCardLayer:d_ptr point:CGPointMake(xPos, yPos) action:cardAction visibleWidth:visibleWidth];
                [layerArray addObject:layer];
            }

            // Mark cards in player's hand that cannot be played by drawing them
            // in grayscale.
            if (playerHand)
            {
                NSArray *contentFilters = nil;
                if (cardAction == nil)
                {
                    CIFilter *filter = [CIFilter filterWithName:@"CIColorControls"];
                    [filter setDefaults];
                    [filter setValue:[NSNumber numberWithFloat:0.0] forKey:@"inputSaturation"];
                    [filter setValue:[NSNumber numberWithFloat:0.1] forKey:@"inputBrightness"];
                    contentFilters = [NSArray arrayWithObject:filter];
                }
                layer.filters = contentFilters;
            }

            cardIndex++;
            xPos += xStep;
        }
    }

    while (cardIndex < [layerArray count])
    {  
        CALayer *layer = [layerArray objectAtIndex:cardIndex];
        layer.hidden = YES;
        cardIndex++;
    }    

}

#define MinX(r) (r.origin.x + width/2)
#define MaxX(r) (r.origin.x + r.size.width - width/2)
#define MinY(r) (r.origin.y + width/2)
#define MaxY(r) (r.origin.y + r.size.height - width/2)

/// Helper function for creating a rounded rectangle.
void RoundRect(CGContextRef ctx, CGRect bounds, CGFloat radius, CGFloat width)
{
    CGContextBeginPath(ctx);
    CGContextMoveToPoint(ctx, MinX(bounds) + radius, MinY(bounds));
    CGContextAddArcToPoint(ctx, MaxX(bounds), MinY(bounds), MaxX(bounds), MaxY(bounds), radius);
    CGContextAddArcToPoint(ctx, MaxX(bounds), MaxY(bounds), MinX(bounds), MaxY(bounds), radius);
    CGContextAddArcToPoint(ctx, MinX(bounds), MaxY(bounds), MinX(bounds), MinY(bounds), radius);
    CGContextAddArcToPoint(ctx, MinX(bounds), MinY(bounds), MaxX(bounds), MinY(bounds), radius);    
    CGContextClosePath(ctx);
}

/// Draws a background layer
- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx
{
    const CGFloat width = 3;
    const CGFloat radius = 10.0;
    const CGFloat lineDash[] = { 5, 4 };

    CGContextSetRGBFillColor(ctx, 0.7, 0.7, 0.7, 0.1);
    CGContextSetRGBStrokeColor(ctx, 0.5, 0.5, 0.5, 0.5);
    CGContextSetLineWidth(ctx, width);
    
    CGRect bounds = layer.bounds;
    RoundRect(ctx, bounds, radius, width);
    CGContextDrawPath(ctx, kCGPathFill);
    
    CGFloat oldW = bounds.size.width;
    bounds.size.width = cardWidth;

    RoundRect(ctx, bounds, radius, width);
    CGContextDrawPath(ctx, kCGPathFillStroke);

    CGContextSetLineDash(ctx, 0.0, lineDash, 2);
    int max = (oldW - cardWidth) / stagger;
    for (int i = 0; i < max; i++)
    {
        int x = MaxX(bounds) + i * stagger;
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, x - radius/2, MinY(bounds));
        CGContextAddArcToPoint(ctx, x + stagger, MinY(bounds), x + stagger, MaxY(bounds), radius);
        CGContextAddArcToPoint(ctx, x + stagger, MaxY(bounds), x - radius/2, MaxY(bounds), radius);
        CGContextAddLineToPoint(ctx, x - radius/2, MaxY(bounds));
        CGContextDrawPath(ctx, kCGPathStroke);
    }    

    NSString *text = nil;
    if (layer == playerSupportLayer || layer == aiSupportLayer)
        text = NSLocalizedString(@"Support\nPile", @"Text drawn at background of support pile");
    else if (layer == playerCombatLayer || layer == aiCombatLayer)
        text = NSLocalizedString(@"Combat\nPile", @"Text drawn at background of combat pile");
    else if (layer == playerDiscardLayer || layer == aiDiscardLayer)
        text = NSLocalizedString(@"Discard\nPile", @"Text drawn at background of discard pile");

    BOOL isHumanPlayer = layer == playerSupportLayer || layer == playerCombatLayer || layer == playerDiscardLayer;
    
    if (text)
    {
        NSGraphicsContext *nsGraphicsContext = [NSGraphicsContext graphicsContextWithGraphicsPort:ctx flipped:NO]; 
        [NSGraphicsContext saveGraphicsState]; 
        [NSGraphicsContext setCurrentContext:nsGraphicsContext]; 

        NSMutableParagraphStyle *paragraphStyle = [[NSMutableParagraphStyle alloc] init];
        [paragraphStyle setAlignment:NSCenterTextAlignment];

        NSDictionary *attrs = [NSDictionary dictionaryWithObjectsAndKeys:
            [NSColor colorWithCalibratedWhite:0.5 alpha:0.8], NSForegroundColorAttributeName,
            paragraphStyle, NSParagraphStyleAttributeName,
            [NSFont fontWithName:@"Lucida Grande" size:12.0], NSFontAttributeName,
            nil];

        int peopleIndex = isHumanPlayer ? human_people : ai_people;
        text = [NSString stringWithFormat:@"%s\n%@", peoples[peopleIndex].name, text];            
        NSSize size = [text sizeWithAttributes:attrs];
        int y = (cardHeight - size.height) / 2;
        [text drawInRect:NSMakeRect(0, y, cardWidth, size.height) withAttributes:attrs];

        [NSGraphicsContext restoreGraphicsState];    
    }
}

/// Keeps track of the layer user clicks so we can process it in mouseUp.
- (void)mouseDown:(NSEvent *)theEvent
{
    // Get mouse location
    NSPoint point = [gameView convertPoint:[theEvent locationInWindow] fromView:NULL];
    
    // Store layer at mouse location
    clickedLayer = [gameLayer hitTest:*(CGPoint *)&point];
}

/// Handles click on the layer
- (void)mouseUp:(NSEvent *)theEvent
{
    // Get mouse location
    NSPoint point = [gameView convertPoint:[theEvent locationInWindow] fromView:NULL];
    
    // Get layer at mouse location
    CALayer *layer = [gameLayer hitTest:*(CGPoint *)&point];

    if (layer == clickedLayer)
    {
        // User clicked a card. If there is action specified for the card, execute it.
        NSDictionary *info = [layer valueForKey:@"info"];
        NSString *action = [info valueForKey:@"action"];
        if (action)
            [self performSelector:NSSelectorFromString(action) withObject:layer];
    }
    
    clickedLayer = nil;
}

/// Called when mouse enters a tracking area. Displays full-size preview of
/// a card when mouse is moved on top of it.
- (void)mouseEntered:(NSEvent *)theEvent;
{
    /// Get layer the mouse is over
    CALayer *layer = [(NSDictionary *)[theEvent userData] valueForKey:@"layer"];

    /// Check if the layer is hidden
    if (layer.hidden)
        return;

    /// Cancel previously ordered hiding of the preview image
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(hidePreview) object:nil];

    /// Get pointer to design of the card
    NSDictionary *infoDict = [layer valueForKey:@"info"];
    design *d_ptr = [[infoDict objectForKey:@"design"] pointerValue];
        
    /// Update and display the preview image
    previewLayer.contents = (id)[imageStore imageForDesign:d_ptr];
    previewLayer.hidden = NO;
}

/// Called when mouse exits a tracking area. Hides the preview image after
/// one second.
- (void)mouseExited:(NSEvent *)theEvent;
{
    [self performSelector:@selector(hidePreview) withObject:nil afterDelay:1.0];
}

/// Hides preview image. Called from mouseExited: via a timer.
- (void)hidePreview
{
    previewLayer.hidden = YES;
}

/// Changes people for the AI or the player. Called when user clicks a
/// leader card before a game has started.
- (void)changePeople:(id)sender
{   
    BOOL isPlayer = sender == layerPlayerLeader;
    int *us = isPlayer ? &human_people : &ai_people;
    int other = isPlayer ? ai_people : human_people;

    // If alt is pressed, randomize
    if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask)
    {
        int previous = *us;
        do {
            *us = rand() % MAX_PEOPLE;
        } while (*us == previous || *us == other); 
    }
    else
    {
        do {
            *us = (*us + 1) % MAX_PEOPLE;
        } while (*us == other);
    }
    
    if (isPlayer)
    {
        [playerSupportLayer setNeedsDisplay];
        [playerCombatLayer setNeedsDisplay];
        [playerDiscardLayer setNeedsDisplay];
    }
    else
    {
        [aiSupportLayer setNeedsDisplay];
        [aiCombatLayer setNeedsDisplay];
        [aiDiscardLayer setNeedsDisplay];
    }
    [self updateTable];
    
    // Update preview image of the leader
    if (*us == MAX_PEOPLE)
        previewLayer.contents = nil;
    else
        previewLayer.contents = (id)[imageStore imageForPeople:*us card:0];
}

/// Called when user clicks on a card that can be retrieved back from the game area.
- (void)retrieveCard:(id)sender
{
    [self saveState];

    // Get card info and retrieve the card
    design *d_ptr = [[sender valueForKeyPath:@"info.design"] pointerValue];
	retrieve_card(&real_game, d_ptr);

	// Redraw stuff
	[self updateAll];
}

/// Called when user clicks on a card that has a special ability.
- (void)cardWasUsed:(id)sender
{
    [self saveState];

    // Get card info and use special ability
    design *d_ptr = [[sender valueForKeyPath:@"info.design"] pointerValue];
	use_special(&real_game, d_ptr);

	// Redraw stuff
    [self updateButtons];
	[self updateAll];
}

/// Called when user clicks on a card that has unsatisfied discard requirements.
- (void)cardWasSatisfied:(id)sender
{
    [self saveState];
    
    // Get card info and satisfy it's discard requirements
    design *d_ptr = [[sender valueForKeyPath:@"info.design"] pointerValue];
    satisfy_discard(&real_game, d_ptr);

	// Redraw stuff
    [self updateButtons];
	[self updateAll];
}

- (CALayer *)createBadgeLayer:(CALayer *)stackLayer count:(int)count
{
    if (count <= 1)
        return nil;
        
    NSImage *badgeImage = [NSImage imageNamed:@"badge1.tiff"];
    CALayer *badge = [CALayer layer];
    badge.contents = (id)[[[badgeImage representations] objectAtIndex:0] CGImage];
    badge.bounds = CGRectMake(0, 0, [badgeImage size].width, [badgeImage size].height);
    CGPoint position = stackLayer.frame.origin;
    position.x += 7;
    position.y += 7;
    badge.position = position;
    badge.shadowRadius = 3.0;
    badge.shadowOffset = CGSizeMake(0, 0);
    badge.shadowColor = CGColorCreateGenericGray(1.0, 1.0);
    badge.shadowOpacity = 1.0;
    badge.contentsGravity = kCAGravityCenter;    
    [gameLayer addSublayer:badge];
    
    CIFilter *filter = [CIFilter filterWithName:@"CIHueAdjust"];
    [filter setValue:[NSNumber numberWithFloat:4.2] forKey:@"inputAngle"];
    badge.filters = [NSArray arrayWithObject:filter];
    
    CATextLayer *badgeText = [CATextLayer layer];
    badgeText.string = [NSString stringWithFormat:@"%d", count];
    badgeText.foregroundColor = CGColorCreateGenericGray(1.0, 1.0);
    CGRect frame = badge.bounds;
    frame.origin.y -= 5;
    badgeText.frame = frame;
    badgeText.fontSize = 12.0;
    badgeText.alignmentMode = kCAAlignmentCenter;
    badgeText.font = @"Lucida Grande";
    badgeText.contentsGravity = kCAGravityCenter;
    [badge addSublayer:badgeText];   
    
    [tableLayers addObject:badge];
    return badge;
}

@end
