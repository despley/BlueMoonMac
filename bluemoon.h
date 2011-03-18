/*
 * Bluemoon AI
 * 
 * Copyright (C) 2007-2008 Keldon Jones
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <ctype.h>
#include <unistd.h>
#include <libintl.h>
#include <locale.h>

/*
 * Default data directory if not specified by Makefile.
 */
#ifndef DATADIR
#define DATADIR "."
#endif

/*
 * Translate message.
 */
#define _(string) gettext(string)

/*
 * Some operating systems do not have strcasecmp, but stricmp instead.
 */
#ifdef win32
#define strcasecmp stricmp
#endif

/*
 * Number of people decks.
 */
#define MAX_PEOPLE      9

/*
 * Number of cards in a deck.
 */
#define DECK_SIZE	31

/*
 * Card icons.
 */
#define ICON_SHIELD_F	0x1
#define ICON_SHIELD_E	0x2
#define ICON_STOP	0x4
#define ICON_RETRIEVE	0x8
#define ICON_PAIR	0x10
#define ICON_FREE	0x20
#define ICON_PROTECTED	0x40
#define ICON_GANG_1	0x80
#define ICON_GANG_2	0x100
#define ICON_GANG_3	0x200
#define ICON_GANG_4	0x400
#define ICON_BLUFF_F    0x800
#define ICON_BLUFF_E    0x1000
#define ICON_BLUFF_N    0x2000

#define ICON_GANG_MASK	(ICON_GANG_1 | ICON_GANG_2 | ICON_GANG_3 | ICON_GANG_4)
#define ICON_BLUFF_MASK (ICON_BLUFF_F | ICON_BLUFF_E | ICON_BLUFF_N)

/*
 * Card types.
 */
#define TYPE_CHARACTER	0x1
#define TYPE_BOOSTER	0x2
#define TYPE_SUPPORT	0x4
#define TYPE_LEADERSHIP	0x8
#define TYPE_INFLUENCE  0x10

/*
 * Special power effects.
 *
 * There are several categories:
 *  1) Cards that ignore opponent's cards or increase my power.
 *  2) Cards that affect dragon attraction upon retreat.
 *  3) "You may not play" or "I may play additional" effects.
 *  4) Drawing, discarding, retrieving cards from either player.
 *  5) Mutant cards that have restrictions on being played.
 *  6) Cards that allow instant dragon attraction.
 *  7) Cards which force opponent to play/discard or retreat.
 *  8) Cards which force opponent to discard/disclose their hand.
 */

/*
 * Category one.
 */
#define S1_IGNORE	0x1
#define S1_INCREASE	0x2

#define S1_ONE_CHAR	0x4
#define S1_ALL_CHAR	0x8
#define S1_ONE_SUPPORT	0x10
#define S1_ALL_SUPPORT	0x20
#define S1_ONE_BOOSTER	0x40
#define S1_ALL_BOOSTER	0x80
#define S1_CATERPILLAR  0x100
#define S1_WITH_ICONS   0x200
#define S1_LEADERSHIP	0x400
#define S1_BLUFF        0x800
#define S1_ALL_CARDS    0x1000

#define S1_TOTAL_POWER	0x2000
#define S1_TOTAL_FIRE   0x4000
#define S1_TOTAL_EARTH  0x8000

#define S1_FIRE_VAL	0x10000
#define S1_EARTH_VAL	0x20000
#define S1_ODD_VAL      0x40000
#define S1_EVEN_VAL     0x80000

#define S1_SPECIAL	0x100000
#define S1_ICONS_ALL	0x200000
#define S1_ICONS_BUT_SP 0x400000
#define S1_ICONS_BUT_S  0x800000

#define S1_BY_FACTOR	0x1000000
#define S1_TO_VALUE	0x2000000
#define S1_BY_VALUE	0x4000000
#define S1_TO_SUM       0x8000000
#define S1_TO_HIGHER    0x10000000

#define S1_EXCEPT_FLIT	0x20000000

/*
 * Category two.
 */
#define S2_I_RETREAT	0x1
#define S2_YOU_RETREAT	0x2

#define S2_ADDITIONAL	0x4
#define S2_FEWER	0x8
#define S2_EXACTLY	0x10
#define S2_NO_MORE_THAN 0x20


/*
 * Category three.
 */
#define S3_YOU_MAY_NOT	0x1
#define S3_I_MAY_PLAY	0x2

#define S3_ADDITIONAL	0x4
#define S3_MORE_THAN	0x8
#define S3_DRAW		0x10
#define S3_TAKE         0x20
#define S3_CALL_BLUFF   0x40

#define S3_CHARACTER	0x80
#define S3_SUPPORT	0x100
#define S3_BOOSTER	0x200
#define S3_LEADERSHIP	0x400
#define S3_COMBAT	0x800

#define S3_HAVE_SPECIAL	0x1000
#define S3_NO_SPECIAL	0x2000
#define S3_WITH_VALUE   0x4000
#define S3_AS_FREE      0x8000

#define S3_SHIP_HAND    0x10000

/*
 * Category four.
 */
#define S4_DRAW           0x1
#define S4_DISCARD        0x2
#define S4_RETRIEVE       0x4
#define S4_REVEAL         0x8
#define S4_SEARCH         0x10
#define S4_UNDRAW_2       0x20
#define S4_SHUFFLE        0x40
#define S4_LOAD           0x80

#define S4_YOUR_HAND      0x100
#define S4_YOUR_CHAR      0x200
#define S4_YOUR_BOOSTER   0x400
#define S4_YOUR_SUPPORT   0x800
#define S4_YOUR_DECK      0x1000

#define S4_MY_CHAR        0x2000
#define S4_MY_BOOSTER     0x4000
#define S4_MY_SUPPORT     0x8000
#define S4_MY_HAND        0x10000
#define S4_MY_DISCARD     0x20000

#define S4_NOT_LAST_CHAR  0x40000
#define S4_WITH_ICON      0x80000
#define S4_ACTIVE         0x100000

#define S4_DISCARD_ONE    0x200000
#define S4_RANDOM_DISCARD 0x400000

#define S4_TO             0x800000

#define S4_ATTACK_AGAIN   0x1000000

#define S4_ON_BOTTOM      0x2000000

#define S4_OPTIONAL       0x4000000
#define S4_IF_FROM_SHIP   0x8000000
#define S4_ALL            0x10000000
#define S4_EITHER         0x20000000

/*
 * Category five.
 */
#define S5_PLAY_ONLY_IF  0x1
#define S5_PLAY_FREE_IF  0x2

#define S5_FIRE_POWER    0x4
#define S5_EARTH_POWER   0x8
#define S5_EITHER_POWER  0x10

#define S5_YOU_ACTIVE    0x20
#define S5_YOU_PLAYED    0x40
#define S5_MY_PLAYED     0x80
#define S5_MY_INFLUENCE  0x100

#define S5_YOU_CHARACTER 0x200
#define S5_YOU_BOOSTER   0x400
#define S5_YOU_SUPPORT   0x800
#define S5_YOU_ICONS     0x1000

#define S5_YOU_DRAGONS   0x2000
#define S5_YOU_HANDSIZE  0x4000

#define S5_ELEMENT_SWAP  0x8000

/*
 * Category six.
 */
#define S6_DISCARD	0x1
#define S6_STORM        0x2

#define S6_FIRE_VALUE	0x4
#define S6_EARTH_VALUE	0x8
#define S6_CHAR		0x10

/*
 * Category seven.
 */
#define S7_PLAY_SUPPORT   0x1
#define S7_PLAY_BOOSTER   0x2

#define S7_DISCARD_FIRE   0x4
#define S7_DISCARD_EARTH  0x8
#define S7_DISCARD_BOTH   0x10
#define S7_DISCARD_EITHER 0x20
#define S7_DISCARD_CHAR   0x40

#define S7_DISCARD_MASK   (S7_DISCARD_FIRE | S7_DISCARD_EARTH | \
                           S7_DISCARD_BOTH | S7_DISCARD_EITHER | \
                           S7_DISCARD_CHAR)

#define S7_CATERPILLAR    0x80
#define S7_FLOOD          0x100

#define S7_OR_RETREAT     0x200
#define S7_OR_DRAGON      0x400

/*
 * Category eight.
 */
#define S8_YOU_DISCARD    0x1
#define S8_YOU_DISCLOSE   0x2

#define S8_TO             0x4

#define S8_OPTIONAL       0x8


/*
 * Special effect timings.
 */
#define TIME_ALWAYS       0
#define TIME_NOW          1
#define TIME_ENDTURN      2
#define TIME_ENDSUPPORT   3
#define TIME_MYTURN       4


/*
 * Card locations.
 */
#define LOC_NONE        0
#define LOC_HAND        1
#define LOC_DRAW        2
#define LOC_COMBAT      3
#define LOC_SUPPORT     4
#define LOC_LEADERSHIP  5
#define LOC_DISCARD     6
#define LOC_INFLUENCE   7
#define LOC_MAX         8

/*
 * Turn phases.
 */
#define PHASE_NONE      0
#define PHASE_START     1
#define PHASE_BEGIN     2
#define PHASE_LEADER    3
#define PHASE_RETREAT   4
#define PHASE_CHAR      5
#define PHASE_SUPPORT   6
#define PHASE_AFTER_SB  7
#define PHASE_ANNOUNCE  8
#define PHASE_REFRESH   9
#define PHASE_END       10
#define PHASE_OVER      11

/*
 * Forward declaration.
 */
struct game;

/*
 * Information about a card design.
 */
typedef struct design
{
	/* Printed values */
	int value[2];

	/* Card type (character, support, etc) */
	int type;

	/* Card icons */
	int icons;

	/* Name of card */
	char *name;

	/* Special text */
	char *text;

	/* Special text priority */
	int special_prio;

	/* Special text category */
	int special_cat;

	/* Special text timing */
	int special_time;

	/* Special text effect */
	int special_effect;

	/* Special text value (usually amount of some sort) */
	int special_value;

	/* People card belongs to (different from deck they are found in) */
	int people;

	/* Index of card design in people */
	int index;

	/* Number of moons on design */
	int moons;

	/* Capacity of a ship */
	int capacity;

} design;

/*
 * Information about a deck for a people.
 */
typedef struct people
{
	/* Name of people */
	char *name;

	/* Deck of card designs */
	design deck[DECK_SIZE];

} people;

/*
 * Information about a card in hand or on the table.
 */
typedef struct card
{
	/* Card owner */
	int owner;

	/* Card design */
	design *d_ptr;

	/* Effective type (usually design's type unless bluffing) */
	int type;

	/* Card special text target (if applicable) */
	design *target;

	/* Card location */
	int where;

	/* Ship card we are sitting on */
	design *ship;

	/* Card is on bottom of draw deck */
	int on_bottom;

	/* Card's effective printed values (almost never modified) */
	int printed[2];

	/* Card values (may be modified from card design's printed values) */
	int value[2];

	/* Card was played this turn */
	int recent;

	/* Card is active */
	int active;

	/* Card is trying to be played as FREE */
	int playing_free;

	/* Card was played as FREE */
	int was_played_free;

	/* Card is played face-down as a bluff */
	int bluff;

	/* Card is a landed ship */
	int landed;

	/* Card's values are ignored */
	int value_ignored;

	/* Card's text is ignored */
	int text_ignored;

	/* Card's text effect is boosted */
	int text_boosted;

	/* Card's effective icons (some or all icons may be ignored) */
	int icons;

	/* Card's special power has been used this turn */
	int used;

	/* Card was randomly picked and may not be "real" */
	int random_fake;

	/* This card's location is known to both players */
	int loc_known;

	/* This card is in the hand, but face-up */
	int disclosed;

} card;

/*
 * Function type for choose callback.
 */
typedef int (*choose_result)(struct game *, int, design **, int, void *);

/*
 * Collection of function pointers for a player's decisions.
 */
typedef struct interface
{
	/* Initialize */
	void (*init)(struct game *g, int who);

	/* Take an action */
	void (*take_action)(struct game *g);

	/* Choose cards */
	void (*choose)(struct game *g, int chooser, int who,
	               design **choices, int num_choices,
		       int min, int max, choose_result callback, void *data,
		       char *prompt);

	/* Call bluff? */
	int (*call_bluff)(struct game *g);

	/* Game over */
	void (*game_over)(struct game *g, int who);

	/* Shutdown */
	void (*shutdown)(struct game *g, int who);

} interface;

/*
 * Information about a player.
 */
typedef struct player
{
	/* People this player is taking */
	people *p_ptr;

	/* Ask player to make decisions */
	interface *control;

	/* Dragons attracted */
	int dragons;

	/* Player won "fourth dragon" victory */
	int instant_win;

	/* Crystals won */
	int crystals;

	/* Player ran out of cards first */
	int no_cards;

	/* Current turn phase */
	int phase;

	/* Deck of cards */
	card deck[DECK_SIZE];

	/* Number of cards in each stack */
	int stack[LOC_MAX];

	/* Last leadership card played */
	design *last_leader;

	/* Last card discarded */
	design *last_discard;

	/* Player has played needed character */
	int char_played;

	/* Minimum total power */
	int min_power;

	/* Cards drawn this turn */
	int cards_drawn;

	/* Index of last card played this phase */
	int last_played;

} player;

/*
 * Current game state.
 */
typedef struct game
{
	/* Two players */
	player p[2];

	/* Current player */
	int turn;

	/* Game is a simulation of the future */
	int simulation;

	/* Who initiated the simulation */
	int sim_turn;

	/* Current fight element */
	int fight_element;

	/* Fight is in progress */
	int fight_started;

	/* Game is over */
	int game_over;

	/* Random event happened recently */
	int random_event;

	/* Random seed */
	unsigned int random_seed;

	/* Seed used to start the game */
	unsigned int start_seed;

} game;



/*
 * External variables.
 */
extern people peoples[MAX_PEOPLE];

extern interface ai_func;


/*
 * External functions.
 */
extern int myrand(unsigned int *seed);
extern int hand_limit(game *g, int who);
extern card *find_card(game *g, int who, design *d_ptr);
extern void move_card(game *g, int who, design *d_ptr, int to, int faceup);
extern design *random_card(game *g, int who, int stack);
extern void reset_cards(game *g);
extern int retrieve_legal(game *g, card *c);
extern void retrieve_card(game *g, design *d_ptr);
extern int card_allowed(game *g, design *d_ptr);
extern int card_eligible(game *g, design *d_ptr);
extern int support_allowed(game *g);
extern int load_allowed(game *g, design *d_ptr);
extern void play_card(game *g, design *d_ptr, int no_effect, int check);
extern int bluff_legal(game *g, int who);
extern void play_bluff(game *g, design *d_ptr);
extern int reveal_bluff(game *g, int who, design *d_ptr);
extern void load_card(game *g, design *d_ptr, design *ship_dptr);
extern void land_ship(game *g, design *d_ptr);
extern int special_possible(game *g, design *d_ptr);
extern void use_special(game *g, design *d_ptr);
extern int satisfy_possible(game *g, design *d_ptr);
extern void satisfy_discard(game *g, design *d_ptr);
extern void start_turn(game *g);
extern void attract_dragon(game *g, int who);
extern void retreat(game *g);
extern int compute_power(game *g, int who);
extern void refresh_phase(game *g);
extern int check_end_support(game *g);
extern void end_support(game *g);
extern void announce_power(game *g, int element);
extern void end_turn(game *g);

extern void read_cards(void);
extern void init_game(game *g, int first);

extern void ai_assist(game *g, char *buf);

extern void message_add(char *msg);
