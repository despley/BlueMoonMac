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

#include "bluemoon.h"

/*
 * Set of peoples.
 */
people peoples[MAX_PEOPLE];


/*
 * Names of icon flags.
 */
static char *icon_name[15] =
{
	"SHIELD_FIRE",
	"SHIELD_EARTH",
	"STOP",
	"RETRIEVE",
	"PAIR",
	"FREE",
	"PROTECTED",
	"GANG_COOL",
	"GANG_TOP",
	"GANG_FUN",
	"GANG_NO",
	"BLUFF_FIRE",
	"BLUFF_EARTH",
	"BLUFF_NONE",
	NULL
};

/*
 * Names of special power flags.
 */
static char *effect_name[9][32] =
{
	/* Category zero doesn't exist */
	{
		NULL,
	},

	/* Category one */
	{
		"IGNORE",
		"INCREASE",
		"ONE_CHAR",
		"ALL_CHAR",
		"ONE_SUPPORT",
		"ALL_SUPPORT",
		"ONE_BOOSTER",
		"ALL_BOOSTER",
		"CATERPILLAR",
		"WITH_ICONS",
		"LEADERSHIP",
		"BLUFF",
		"ALL_CARDS",
		"TOTAL_POWER",
		"TOTAL_FIRE",
		"TOTAL_EARTH",
		"FIRE_VAL",
		"EARTH_VAL",
		"ODD_VAL",
		"EVEN_VAL",
		"SPECIAL",
		"ICONS_ALL",
		"ICONS_BUT_SP",
		"ICONS_BUT_S",
		"BY_FACTOR",
		"TO_VALUE",
		"BY_VALUE",
		"TO_SUM",
		"TO_HIGHER",
		"EXCEPT_FLIT",
		NULL,
	},

	/* Category two */
	{
		"I_RETREAT",
		"YOU_RETREAT",
		"ADDITIONAL",
		"FEWER",
		"EXACTLY",
		"NO_MORE_THAN",
		NULL,
	},

	/* Category three */
	{
		"YOU_MAY_NOT",
		"I_MAY_PLAY",
		"ADDITIONAL",
		"MORE_THAN",
		"DRAW",
		"TAKE",
		"CALL_BLUFF",
		"CHARACTER",
		"SUPPORT",
		"BOOSTER",
		"LEADERSHIP",
		"COMBAT",
		"HAVE_SPECIAL",
		"NO_SPECIAL",
		"WITH_VALUE",
		"AS_FREE",
		"SHIP_HAND",
		NULL,
	},
	
	/* Category four */
	{
		"DRAW",
		"DISCARD",
		"RETRIEVE",
		"REVEAL",
		"SEARCH",
		"UNDRAW_2",
		"SHUFFLE",
		"LOAD",
		"YOUR_HAND",
		"YOUR_CHAR",
		"YOUR_BOOSTER",
		"YOUR_SUPPORT",
		"YOUR_DECK",
		"MY_CHAR",
		"MY_BOOSTER",
		"MY_SUPPORT",
		"MY_HAND",
		"MY_DISCARD",
		"NOT_LAST_CHAR",
		"WITH_ICON",
		"ACTIVE",
		"DISCARD_ONE",
		"RANDOM_DISCARD",
		"TO",
		"ATTACK_AGAIN",
		"ON_BOTTOM",
		"OPTIONAL",
		"IF_FROM_SHIP",
		"ALL",
		"EITHER",
		NULL,
	},

	/* Category five */
	{
		"PLAY_ONLY_IF",
		"PLAY_FREE_IF",
		"FIRE_POWER",
		"EARTH_POWER",
		"EITHER_POWER",
		"YOU_ACTIVE",
		"YOU_PLAYED",
		"MY_PLAYED",
		"MY_INFLUENCE",
		"YOU_CHARACTER",
		"YOU_BOOSTER",
		"YOU_SUPPORT",
		"YOU_ICONS",
		"YOU_DRAGONS",
		"YOU_HANDSIZE",
		"ELEMENT_SWAP",
		NULL,
	},

	/* Category six */
	{
		"DISCARD",
		"STORM",
		"FIRE_VALUE",
		"EARTH_VALUE",
		"CHAR",
		NULL,
	},

	/* Category seven */
	{
		"PLAY_SUPPORT",
		"PLAY_BOOSTER",
		"DISCARD_FIRE",
		"DISCARD_EARTH",
		"DISCARD_BOTH",
		"DISCARD_EITHER",
		"DISCARD_CHAR",
		"CATERPILLAR",
		"FLOOD",
		"OR_RETREAT",
		"OR_DRAGON",
		NULL,
	},

	/* Category eight */
	{
		"YOU_DISCARD",
		"YOU_DISCLOSE",
		"TO",
		"OPTIONAL",
		NULL,
	},
};

/*
 * Lookup an effect code.
 */
static int lookup_effect(char *ptr, int cat)
{
	int i = 0;

	/* Loop over effects */
	while (effect_name[cat][i])
	{
		/* Check this effect */
		if (!strcmp(effect_name[cat][i], ptr)) return 1 << i;

		/* Next effect */
		i++;
	}

	/* No effect found */
	printf(_("No effect matching '%s'!\n"), ptr);
	exit(1);
}

/*
 * Read card designs from a text file.
 */
void read_cards(void)
{
	FILE *fff;
	char buf[1024], *ptr;
	int num_people = 0, num_design = 0;
	design *deck = NULL, *d_ptr = NULL;
	int i, effect, cat;

	/* Open card design file */
	fff = fopen(DATADIR "/cards.txt", "r");

	/* Check for error */
	if (!fff)
	{
		/* Try reading from current directory instead */
		fff = fopen("cards.txt", "r");
	}

	/* Check for error */
	if (!fff)
	{
		/* Print error and exit */
		perror("cards.txt");
		exit(1);
	}

	/* Loop over entire file */
	while (1)
	{
		/* Read a line */
		fgets(buf, 1024, fff);

		/* Check for end of file */
		if (feof(fff)) break;

		/* Strip newline */
		buf[strlen(buf) - 1] = '\0';

		/* Skip comments and blank lines */
		if (!buf[0] || buf[0] == '#') continue;

		/* Switch of type of line */
		switch (buf[0])
		{
			/* New people */
			case 'P':

				/* Read name */
				peoples[num_people].name = strdup(buf + 2);

				/* Pointer to current deck */
				deck = peoples[num_people].deck;

				/* Start at first design */
				num_design = 0;

				/* Count peoples */
				num_people++;

				break;

			/* New card */
			case 'N':

				/* Current design pointer */
				d_ptr = &deck[num_design];

				/* Read name */
				d_ptr->name = strdup(buf + 2);

				/* Count designs */
				num_design++;

				break;

			/* Strength values */
			case 'V':

				/* Get first value string */
				ptr = strtok(buf + 2, ":");

				/* Read fire value */
				d_ptr->value[0] = strtol(ptr, NULL, 0);

				/* Get next value */
				ptr = strtok(NULL, ":");

				/* Read earth value */
				d_ptr->value[1] = strtol(ptr, NULL, 0);

				break;

			/* Card type */
			case 'T':

				/* Read type */
				d_ptr->type = strtol(buf + 2, NULL, 0);

				break;

			/* Flags */
			case 'F':

				/* Get first flag */
				ptr = strtok(buf + 2, " |");

				/* Loop over flags */
				while (ptr)
				{
					/* Check each icon type */
					for (i = 0; icon_name[i]; i++)
					{
						/* Check this type */
						if (!strcmp(ptr, icon_name[i]))
						{
							/* Set icon flag */
							d_ptr->icons |= 1 << i;

							/* Done looking */
							break;
						}
					}

					/* Check for no match */
					if (!icon_name[i])
					{
						/* Error */
						printf("Unknown icon %s!\n",
						       ptr);

						/* Exit */
						exit(1);
					}

					/* Get next flag */
					ptr = strtok(NULL, " |");
				}

				break;

			/* Special text */
			case 'S':

				/* Read text */
				d_ptr->text = strdup(buf + 2);

				break;

			/* Special effect */
			case 'E':

				/* Get effect category string */
				ptr = strtok(buf + 2, ":");

				/* Read effect category */
				cat = strtol(ptr, NULL, 0);

				/* Store category */
				d_ptr->special_cat = cat;

				/* Get priority string */
				ptr = strtok(NULL, ":");

				/* Read priority */
				d_ptr->special_prio = strtol(ptr, NULL, 0);

				/* Get timing string */
				ptr = strtok(NULL, ":");

				/* Read timing */
				d_ptr->special_time = strtol(ptr, NULL, 0);

				/* Clear effect code */
				effect = 0;

				/* Read effect flags */
				while ((ptr = strtok(NULL, "|: ")))
				{
					/* Check for end of flags */
					if (isdigit(*ptr)) break;

					/* Lookup effect code */
					effect |= lookup_effect(ptr, cat);
				}

				/* Store effect code */
				d_ptr->special_effect = effect;

				/* Read effect value */
				d_ptr->special_value = strtol(ptr, NULL, 0);

				break;

			/* Miscellaneous info */
			case 'M':

				/* Get people string */
				ptr = strtok(buf + 2, ":");

				/* Read people */
				d_ptr->people = strtol(ptr, NULL, 0);

				/* Get index string */
				ptr = strtok(NULL, ":");

				/* Read index */
				d_ptr->index = strtol(ptr, NULL, 0);

				/* Get moon string */
				ptr = strtok(NULL, ":");

				/* Read moons */
				d_ptr->moons = strtol(ptr, NULL, 0);

				break;

			/* Capacity (for ships) */
			case 'C':

				/* Read capacity */
				d_ptr->capacity = strtol(buf + 2, NULL, 0);

				break;
		}
	}

	/* Close card design file */
	fclose(fff);
}

/*
 * Initialize a game.
 */
void init_game(game *g, int first)
{
	player *p;
	design *d_ptr;
	card *c;
	int i, j;

	/* Store game start random seed */
	g->start_seed = g->random_seed;

	/* printf("start seed: %u\n", g->start_seed); */

	/* Game not over yet */
	g->game_over = 0;

	/* Game is not a simulation */
	g->simulation = 0;

	/* No fight started or element chosen yet */
	g->fight_element = g->fight_started = 0;

	/* Loop over players */
	for (i = 0; i < 2; i++)
	{
		/* Get player pointer */
		p = &g->p[i];

		/* Loop over stacks */
		for (j = 0; j < LOC_MAX; j++) p->stack[j] = 0;

		/* Player has no dragons */
		p->dragons = 0;

		/* Player did not win instant victory */
		p->instant_win = 0;

		/* Player didn't run out of cards */
		p->no_cards = 0;

		/* Player may have no crystals */
		if (first) p->crystals = 0;

		/* Clear minimum power */
		p->min_power = 0;

		/* Get leader card */
		c = &p->deck[0];

		/* Set card design */
		c->d_ptr = &p->p_ptr->deck[0];

		/* Put in leadership pile */
		c->where = LOC_LEADERSHIP;

		/* Card not recently played */
		c->recent = 0;

		/* Remember last leadership card played */
		p->last_leader = c->d_ptr;

		/* No last discard yet */
		p->last_discard = NULL;

		/* Size of draw deck */
		p->stack[LOC_DRAW] = DECK_SIZE - 1;

		/* Loop over cards */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Set owner */
			c->owner = i;

			/* Set card design */
			c->d_ptr = &p->p_ptr->deck[j];

			/* Set card type */
			c->type = c->d_ptr->type;

			/* No target */
			c->target = NULL;

			/* Put card in draw pile */
			c->where = LOC_DRAW;

			/* Not on a ship */
			c->ship = NULL;

			/* Card is not stuck at bottom of draw pile */
			c->on_bottom = 0;

			/* Card is not a landed ship */
			c->landed = 0;

			/* Cards begin inactive */
			c->active = 0;

			/* Card is not played as free */
			c->was_played_free = 0;
			c->playing_free = 0;

			/* Card is not a bluff */
			c->bluff = 0;

			/* Card was not recently played */
			c->recent = 0;

			/* Cards are not fake */
			c->random_fake = 0;

			/* Card locations are unknown */
			c->loc_known = 0;
			c->disclosed = 0;
		}

		/* Draw six cards */
		for (j = 0; j < 6; j++)
		{
			/* Pick random card */
			d_ptr = random_card(g, i, LOC_DRAW);

			/* Move to hand */
			move_card(g, i, d_ptr, LOC_HAND, 0);
		}
	}

	/* Pick a starting player (randomly) */
	g->turn = myrand(&g->random_seed) % 2;

	/* Have start player begin */
	g->p[g->turn].phase = PHASE_START;
	g->p[!g->turn].phase = PHASE_NONE;

	/* Reset card flags */
	reset_cards(g);
}
