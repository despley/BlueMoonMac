/**
 * @file
 * Interface of class BMMainWindowController.
 */

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import "bluemoon.h"

#define kNumPeople 10   ///< Amount of various people in the game.
#define kNumCards 128   ///< Largest possible card index in the game.

@class BMCardImageStore;

/**
 * This class manages the main game window and takes care that information
 * displayed in the main window is in sync with the game engine.
 */
@interface BMMainWindowController : NSWindowController 
{
    // Views of the main window
    IBOutlet NSTextView *messageView;   ///< Text view used to show messages from the game engine
    IBOutlet NSView *gameView;          ///< Custom view showing the game area and cards
    IBOutlet NSView *previewView;       ///< View that contains large card preview
    IBOutlet NSTabView *buttonTabView;  ///< Tabless tab view that contains various buttons
    IBOutlet NSButton *retreatButton;   ///< Button used to retreat from fight
    IBOutlet NSButton *fireButton;      ///< Button used to announce fight in fire
    IBOutlet NSButton *earthButton;     ///< Button used to announce fight in earth
    IBOutlet NSButton *undoButton;      ///< Button used to undo a turn

    // Layers for game area background
    CALayer *gameLayer;                 ///< Layer for the game area and cards (located in gameView)
    CALayer *playerSupportLayer;        ///< Layer for background of player's support area
    CALayer *playerCombatLayer;         ///< Layer for background of player's combat area
    CALayer *playerDiscardLayer;        ///< Layer for background of player's discard area
    CALayer *aiSupportLayer;            ///< Layer for background of AI's support area
    CALayer *aiCombatLayer;             ///< Layer for background of AI's combat area
    CALayer *aiDiscardLayer;            ///< Layer for background of AI's discard area

    // Layers for cards
    CALayer *previewLayer;              ///< Large card preview layer (located in previewView)
    CALayer *deckPlayer;                ///< Layer for player's deck
    CALayer *layerPlayerDiscard;        ///< Layer for player's topmost discarded card
    CALayer *layerPlayerLeader;         ///< Layer for player's leader or topmost leadership card
    NSMutableArray *playerCardLayers;   ///< Layers for cards in player's hand
    CALayer *deckAI;                    ///< Layer for AI's deck
    CALayer *layerAiDiscard;            ///< Layer for AI's topmost discarded card
    CALayer *layerAiLeader;             ///< Layer for AI's leader or leadership card
    NSMutableArray *aiCardLayers;       ///< Layers for cards in AI's hand
    NSMutableArray *tableLayers;        ///< Layers for all other cards in the game area
    CALayer *clickedLayer;              ///< Layer that was clicked with mouse

    // Miscellanous info
    int cardWidth;              ///< Width of one card in game area
    int cardHeight;             ///< Height of one card in game area
    NSString *statusText;       ///< Current status text;

    // Game state
    game real_game;             ///< Current (real) game state
    int player_us;              ///< Player we're playing as
    int human_people;           ///< People index for human player
    int ai_people;              ///< People index for ai player
    BOOL backup_set;            ///< If YES, there is a backup that can be restored
    game backup;                ///< Back-up game to restore from when undo-ing

    /// Class managing card images
    BMCardImageStore *imageStore;

    /// Image used for card backgrounds
    CGImageRef cardBackImage;
}

// Properties
@property(copy) NSString *statusText;
@property(readonly) BOOL gameStarted;

// Handlers for UI actions
- (IBAction)onNewGame:(id)sender;
- (IBAction)retreatButtonWasClicked:(id)sender;
- (IBAction)announceButtonWasClicked:(id)sender;
- (IBAction)undoButtonWasClicked:(id)sender;
- (IBAction)cardWasClicked:(CALayer *)layer;
- (IBAction)startButtonWasClicked:(id)sender;
- (IBAction)debugDiscloseHand:(id)sender;
- (IBAction)debugSelectGame:(id)sender;
- (IBAction)showPreferencesWindow:(id)sender;

- (void)updateButtons;
- (void)updateAll;


- (void)addMessage:(NSString *)message;
- (void)handleEndTurn;

@end
