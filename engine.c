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

/* Forward declaration */
static void notice_effect_1(game *g);

/*
 * Return a random number using the given argument as a seed.
 *
 * Algorithm from rand() manpage.
 */
int myrand(unsigned int *seed)
{
	*seed = *seed * 1103515245 + 12345;
	return ((unsigned)(*seed/65536) % 32768);
}

/*
 * Return a player's hand limit.
 *
 * Normally this is infinite, but some cards prevent you from "taking cards
 * into your hand".
 */
int hand_limit(game *g, int who)
{
	player *p;
	card *c;
	int i, limit = 99;

	/* Get opponent pointer */
	p = &g->p[!who];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with text ignored */
		if (c->text_ignored) continue;

		/* Skip cards without category 3 effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Check for "you may not take" */
		if (c->d_ptr->special_effect == (S3_YOU_MAY_NOT | S3_TAKE))
		{
			/* Reduce limit */
			if (limit > c->d_ptr->special_value)
			{
				/* Set to lower value */
				limit = c->d_ptr->special_value;
			}
		}
	}

	/* Return hand limit */
	return limit;
}

/*
 * Find the given card design in a player's deck.
 */
card *find_card(game *g, int who, design *d_ptr)
{
	player *p = &g->p[who];
	card *c;
	int i;

	/* Loop over player's cards */
	for (i = 0; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Check design */
		if (c->d_ptr == d_ptr) return c;
	}

	/* Design not found */
	return NULL;
}

/*
 * Deactivate a card.
 *
 * Clear several flags.
 */
void deactivate_card(card *c)
{
	/* Clear active card */
	c->active = 0;

	/* Not played recently */
	c->recent = 0;

	/* No special text target */
	c->target = NULL;

	/* Card no longer played as free */
	c->was_played_free = 0;

	/* Card is not used */
	c->used = 0;
}

/*
 * Check if draw pile is empty and all cards in discard pile are known to
 * be there.  If this is true, then all cards in the hand will also be known.
 */
static void check_all_known(game *g, int who)
{
	player *p;
	card *c;
	int i;

	/* Get player pointer */
	p = &g->p[who];

	/* Check for empty draw pile */
	if (p->stack[LOC_DRAW]) return;

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not in discard pile */
		if (c->where != LOC_DISCARD) continue;

		/* Check for discard card not known */
		if (!c->loc_known) return;
	}

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not in hand pile */
		if (c->where != LOC_HAND) continue;

		/* Set location as known */
		c->loc_known = 1;
	}
}

/*
 * Check if any "random_fake" flags can be removed from cards.
 */
static void remove_fake(game *g, int who)
{
	player *p;
	card *c;
	int i;

	/* No flags to remove in unsimulated games */
	if (!g->simulation) return;

	/* Get player pointer */
	p = &g->p[who];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip non-fake cards */
		if (!c->random_fake) continue;

		/* Check for empty previous pile */
		if (!p->stack[c->random_fake])
		{
			/* Clear flag */
			c->random_fake = 0;
		}
	}
}

/*
 * Move a given card to a new pile.
 */
void move_card(game *g, int who, design *d_ptr, int to, int faceup)
{
	char msg[1024];
	player *p;
	card *c;
	int i;

	/* Get player pointer */
	p = &g->p[who];

	/* Find card */
	c = find_card(g, who, d_ptr);

	/* Reduce from stack by one */
	p->stack[c->where]--;

	/* Move card */
	c->where = to;

	/* Increase destination stack by one */
	p->stack[to]++;

	/* Moving cards always deactivates them */
	deactivate_card(c);

	/* Moved cards lose disclosed flag */
	c->disclosed = 0;

	/* Check for last discard moved from discard pile */
	if (p->last_discard == d_ptr && to != LOC_DISCARD)
	{
		/* Pick random card from discard pile instead */
		p->last_discard = random_card(g, who, LOC_DISCARD);
	}

	/* Track last card moved to leadership pile */
	if (to == LOC_LEADERSHIP) p->last_leader = d_ptr;

	/* Track last card moved to discard pile */
	if (to == LOC_DISCARD) p->last_discard = d_ptr;

	/* Message */
	if (!g->simulation && faceup)
	{
		/* Switch on destination */
		switch (to)
		{
			/* Hand */
			case LOC_HAND:

				/* Format message */
				sprintf(msg, _("%s takes %s into hand.\n"),
				        _(p->p_ptr->name), _(d_ptr->name));
				break;

			/* Draw pile */
			case LOC_DRAW:

				/* Format message */
				sprintf(msg, _("Moving %s to draw pile.\n"),
				        _(d_ptr->name));
				break;

			/* Combat area */
			case LOC_COMBAT:

				/* Format message */
				sprintf(msg, _("Moving %s to combat area.\n"),
				        _(d_ptr->name));
				break;

			/* Support area */
			case LOC_SUPPORT:

				/* Format message */
				sprintf(msg, _("Moving %s to support area.\n"),
				        _(d_ptr->name));
				break;

			/* Leadership */
			case LOC_LEADERSHIP:

				/* Format message */
				sprintf(msg,
				        _("%s moves %s to leadership area.\n"),
				        _(p->p_ptr->name), _(d_ptr->name));
				break;

			/* Discard */
			case LOC_DISCARD:

				/* Format message */
				sprintf(msg,
				        _("%s moves %s to discard pile.\n"),
				        _(p->p_ptr->name), _(d_ptr->name));
				break;
		}

		/* Send message */
		message_add(msg);
	}

	/* If move is "face-up", card's location is known */
	if (faceup) c->loc_known = 1;

	/* Check for running out of cards first */
	if (p->stack[LOC_HAND] + p->stack[LOC_DRAW] == 0)
	{
		/* Check for first */
		if (!g->p[!who].no_cards) p->no_cards = 1;
	}

	/* Check for discarding ship */
	if (c->d_ptr->capacity && to == LOC_DISCARD)
	{
		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Check for on ship */
			if (c->ship == d_ptr)
			{
				/* Cards on ship are discarded too */
				move_card(g, who, c->d_ptr, to, faceup);

				/* Card is no longer on ship */
				c->ship = NULL;
			}
		}
	}

	/* Check for all card locations known */
	check_all_known(g, who);

	/* Check for removable fake flags */
	remove_fake(g, who);
}

/*
 * Choose a card at random from one pile.
 */
design *random_card(game *g, int who, int stack)
{
	player *p;
	int i, n, n1 = 0, n2 = 0;
	card *c;

	/* Get player pointer */
	p = &g->p[who];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards in wrong pile */
		if (c->where != stack) continue;

		/* Check for "forced at bottom" */
		if (c->on_bottom)
		{
			/* Count bottom cards */
			n2++;
		}
		else
		{
			/* Count normal cards */
			n1++;
		}
	}

	/* Check for no cards */
	if (!(n1 + n2)) return NULL;

	/* Get a random card */
	if (n1)
	{
		/* Get random normal card */
		n = myrand(&g->random_seed) % n1;
	}
	else
	{
		/* Get random bottom card */
		n = myrand(&g->random_seed) % n2;
	}

	/* Loop over player's cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards in incorrect stack */
		if (c->where != stack) continue;

		/* Skip cards on bottom if there are normal cards */
		if (c->on_bottom && n1) continue;

		/* Stop at n'th card */
		if (!n) break;

		/* Count down cards */
		n--;
	}

	/* Check for simulation */
	if (g->simulation)
	{
		/* Mark card */
		c->random_fake = c->where;
	}

	/* Note random event */
	g->random_event = 1;

	/* Remove bottom flag */
	c->on_bottom = 0;

	/* Return design pointer */
	return c->d_ptr;
}

/*
 * Choose a card at random from the draw pile.
 *
 * Certain opposing effects prevent this, so we may return NULL even if
 * there are cards in the pile.
 */
design *random_draw(game *g, int who)
{
	player *p, *opp;
	design *d_ptr;
	card *c;
	int effect, value;
	int i;

	/* Get player pointer */
	p = &g->p[who];

	/* Choose a random card */
	d_ptr = random_card(g, who, LOC_DRAW);

	/* Check for no cards available */
	if (!d_ptr) return NULL;

	/* Get opponent pointer */
	opp = &g->p[!who];

	/* Loop over opponent's cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with ignored text */
		if (c->text_ignored) continue;

		/* Skip cards with non-category 3 effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Get card's effect code */
		effect = c->d_ptr->special_effect;
		value = c->d_ptr->special_value;

		/* Skip cards that aren't "you may not draw" */
		if (effect != (S3_YOU_MAY_NOT | S3_DRAW)) continue;

		/* Check for too many cards drawn */
		if (p->cards_drawn >= value) return NULL;
	}

	/* One more card drawn this turn */
	p->cards_drawn++;

	/* Return card selected */
	return d_ptr;
}

/*
 * Try to draw a card.
 */
int draw_card(game *g)
{
	player *p;
	design *d_ptr;

	/* Player pointer */
	p = &g->p[g->turn];

	/* Pick a random card */
	d_ptr = random_draw(g, g->turn);

	/* No cards available */
	if (!d_ptr) return 0;

	/* Check hand limit */
	if (p->stack[LOC_HAND] >= hand_limit(g, g->turn)) return 0;

	/* Put card in hand */
	move_card(g, g->turn, d_ptr, LOC_HAND, 0);

	/* Success */
	return 1;
}

/*
 * Reset card flags.
 */
static void reset_card(card *c)
{
	/* Clear ignored flags */
	c->value_ignored = 0;
	c->text_ignored = 0;

	/* Clear boosted flag */
	c->text_boosted = 0;

	/* Reset icons */
	c->icons = c->d_ptr->icons;

	/* Check for bluff card */
	if (c->bluff)
	{
		/* Set printed values to 2 */
		c->printed[0] = c->printed[1] = 2;

		/* Set effective values to 2 */
		c->value[0] = c->value[1] = 2;

		/* Card's text is ignored */
		c->text_ignored = 1;
	}
	else
	{
		/* Reset printed values */
		c->printed[0] = c->d_ptr->value[0];
		c->printed[1] = c->d_ptr->value[1];

		/* Reset effective values */
		c->value[0] = c->d_ptr->value[0];
		c->value[1] = c->d_ptr->value[1];
	}
}

/*
 * Reset our flags on the current cards.
 *
 * Here we clear any "ignored" flags, reset card power values to their
 * printed value, etc.
 */
void reset_cards(game *g)
{
	player *p;
	card *c;
	int i, j;

	/* Loop over each player */
	for (i = 0; i < 2; i++)
	{
		/* Get player pointer */
		p = &g->p[i];

		/* Loop over cards */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Cards in discard pile may be skipped */
			if (c->where == LOC_DISCARD) continue;
			
			/* Reset card */
			reset_card(c);
		}

		/* Clear player's minimum power level */
		p->min_power = 0;
	}
}

/*
 * Handle an "ignore" special power.
 */
static void s1_ignore_card(card *c, int effect)
{
	/* Check for ignore icons except STOP */
	if (effect & S1_ICONS_BUT_S)
	{
		/* Remove icons */
		c->icons &= ICON_STOP;

		/* Card cannot be played as FREE */
		c->playing_free = 0;
	}

	/* Cards with PROTECTED icon cannot be ignored */
	if (c->icons & ICON_PROTECTED) return;

	/* Check for "except Flit character cards" restriction */
	if (effect & S1_EXCEPT_FLIT)
	{
		/* Check for Flit character */
		if (c->type == TYPE_CHARACTER && c->d_ptr->people == 3)
		{
			/* Do not ignore */
			return;
		}
	}

	/* Check for "with icons" restriction */
	if (effect & S1_WITH_ICONS)
	{
		/* Check for no icons on card */
		if (!c->d_ptr->icons && !c->playing_free) return;
	}

	/* Check for ignore odd values */
	if (effect & (S1_ODD_VAL))
	{
		/* Check fire value */
		if (c->printed[0] % 2 == 1)
		{
			/* Ignore printed fire value */
			c->printed[0] = c->value[0] = 0;
		}

		/* Check earth value */
		if (c->printed[1] % 2 == 1)
		{
			/* Ignore printed earth value */
			c->printed[1] = c->value[1] = 0;
		}
	}

	/* Check for ignore even values */
	if (effect & (S1_EVEN_VAL))
	{
		/* Check fire value */
		if (c->printed[0] % 2 == 0)
		{
			/* Ignore printed fire value */
			c->printed[0] = c->value[0] = 0;
		}

		/* Check earth value */
		if (c->printed[1] % 2 == 0)
		{
			/* Ignore printed earth value */
			c->printed[1] = c->value[1] = 0;
		}
	}

	/* Check for ignore value */
	if (effect & (S1_FIRE_VAL | S1_EARTH_VAL)) c->value_ignored = 1;

	/* Check for ignore special text */
	if (effect & S1_SPECIAL) c->text_ignored = 1;

	/* Check for ignore icons */
	if (effect & S1_ICONS_ALL)
	{
		/* Clear icons */
		c->icons = 0;

		/* Card cannot be played as FREE */
		c->playing_free = 0;
	}

	/* Check for ignore icons except STOP and PROTECTED */
	if (effect & S1_ICONS_BUT_SP)
	{
		/* Clear most icons */
		c->icons &= ICON_STOP | ICON_PROTECTED;

		/* Card cannot be played as FREE */
		c->playing_free = 0;
	}
}

/*
 * Handle an "increase" special power.
 */
static void s1_boost_card(card *c, int effect, int value)
{
	/* Check for boosting fire */
	if (effect & S1_FIRE_VAL)
	{
		/* Check for increase by factor */
		if (effect & S1_BY_FACTOR) c->value[0] *= value;

		/* Check for increase to value */
		if (effect & S1_TO_VALUE)
		{
			/* Increase if lower */
			if (c->value[0] < value) c->value[0] = value;
		}

		/* Check for increase by value */
		if (effect & S1_BY_VALUE) c->value[0] += value;

		/* Check for increase to sum */
		if (effect & S1_TO_SUM) c->value[0] += c->printed[1];

		/* Check for increase to higher */
		if (effect & S1_TO_HIGHER)
		{
			/* Check for higher */
			if (c->printed[1] > c->value[0])
			{
				/* Increase value */
				c->value[0] = c->printed[1];
			}
		}
	}

	/* Check for boosting earth */
	if (effect & S1_EARTH_VAL)
	{
		/* Check for increase by factor */
		if (effect & S1_BY_FACTOR) c->value[1] *= value;

		/* Check for increase to value */
		if (effect & S1_TO_VALUE)
		{
			/* Increase if lower */
			if (c->value[1] < value) c->value[1] = value;
		}

		/* Check for increase by value */
		if (effect & S1_BY_VALUE) c->value[1] += value;

		/* Check for increase to sum */
		if (effect & S1_TO_SUM) c->value[1] += c->printed[0];

		/* Check for increase to higher */
		if (effect & S1_TO_HIGHER)
		{
			/* Check for higher */
			if (c->printed[0] > c->value[1])
			{
				/* Increase value */
				c->value[1] = c->printed[0];
			}
		}
	}

	/* Check for boosting special text */
	if (effect & S1_SPECIAL) c->text_boosted = 1;
}

/*
 * Callback used when asking for a card to be boosted.
 */
static int boost_callback(game *g, int who, design **list, int num, void *data)
{
	design *d_ptr = (design *)data;
	card *c;
	int effect, value;
	char msg[1024];

	/* Get effect code and value */
	effect = d_ptr->special_effect;
	value = d_ptr->special_value;

	/* Boosting anything but one card is illegal */
	if (num != 1) return 0;

	/* Get boosting card */
	c = find_card(g, who, d_ptr);

	/* Remember target */
	c->target = list[0];

	/* Notice special text */
	notice_effect_1(g);

	/* Check for simulation */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("Boosting %s with %s.\n"), _(c->target->name),
						          _(d_ptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Success */
	return 1;
}

/*
 * Handle a category 1 effect.
 */
static void handle_effect_1(game *g, int who, design *d_ptr)
{
	player *p;
	card *c, *t;
	int effect, value;
	int i;

	/* Get effect code and value */
	effect = d_ptr->special_effect;
	value = d_ptr->special_value;

	/* Handle "ignore" effects */
	if (effect & S1_IGNORE)
	{
		/* Get opponent pointer */
		p = &g->p[!who];

		/* Loop over opponent cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Check for all cards */
			if (effect & S1_ALL_CARDS)
			{
				/* Ignore this card */
				s1_ignore_card(c, effect);
			}

			/* Check for leadership card */
			if (c->type == TYPE_LEADERSHIP &&
			    (effect & S1_LEADERSHIP))
			{
				/* Ignore this card */
				s1_ignore_card(c, effect);
			}

			/* Check for character card */
			if (c->type == TYPE_CHARACTER &&
			    (effect & S1_ALL_CHAR))
			{
				/* Ignore this card */
				s1_ignore_card(c, effect);
			}

			/* Check for booster card */
			if (c->type == TYPE_BOOSTER &&
			    (effect & S1_ALL_BOOSTER))
			{
				/* Ignore this card */
				s1_ignore_card(c, effect);
			}

			/* Check for support card */
			if (c->type == TYPE_SUPPORT &&
			    (effect & S1_ALL_SUPPORT))
			{
				/* Ignore this card */
				s1_ignore_card(c, effect);
			}
		}

		/* Done with ignoring */
		return;
	}

	/* We increase our own cards */
	p = &g->p[who];

	/* Check for total power increase */
	if (effect & S1_TOTAL_POWER)
	{
		/* Increase minimum power */
		if (p->min_power < value) p->min_power = value;

		/* Done */
		return;
	}

	/* Check for total power in element increase */
	if (effect & S1_TOTAL_FIRE)
	{
		/* Check for fight in fire */
		if (g->fight_started && !g->fight_element)
		{
			/* Increase minimum power */
			if (p->min_power < value) p->min_power = value;
		}

		/* Done */
		return;
	}

	/* Check for total power in element increase */
	if (effect & S1_TOTAL_EARTH)
	{
		/* Check for fight in earth */
		if (g->fight_started && g->fight_element)
		{
			/* Increase minimum power */
			if (p->min_power < value) p->min_power = value;
		}

		/* Done */
		return;
	}

	/* Check for "one card" booster effects */
	if (effect & (S1_ONE_CHAR | S1_ONE_BOOSTER | S1_ONE_SUPPORT))
	{
		/* Get card with effect */
		c = find_card(g, who, d_ptr);

		/* Check for target already set */
		if (c->target)
		{
			/* Get target card */
			t = find_card(g, who, c->target);

			/* Check for inactive */
			if (!t->active)
			{
				/* Clear target */
				c->target = NULL;
			}
			else
			{
				/* Apply effect to target card */
				s1_boost_card(t, effect, value);

				/* Done */
				return;
			}
		}

		/* Done */
		return;
	}

	/* Loop over our cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Inactive cards cannot be boosted */
		if (!c->active) continue;

		/* Check for character card */
		if (c->where == LOC_COMBAT &&
		    c->type == TYPE_CHARACTER &&
		    (effect & S1_ALL_CHAR))
		{
			/* Boost this card */
			s1_boost_card(c, effect, value);
		}

		/* Check for booster card */
		if (c->where == LOC_COMBAT &&
		    c->type == TYPE_BOOSTER &&
		    (effect & S1_ALL_BOOSTER))
		{
			/* Boost this card */
			s1_boost_card(c, effect, value);
		}

		/* Check for support card */
		if (c->where == LOC_SUPPORT &&
		    (effect & S1_ALL_SUPPORT))
		{
			/* Boost this card */
			s1_boost_card(c, effect, value);
		}

		/* Check for bluff card */
		if (c->bluff && (effect & S1_BLUFF))
		{
			/* Boost this card */
			s1_boost_card(c, effect, value);
		}
	}
}

/*
 * There are currently 3 "priority 2" cards with a "category 1" ignore
 * effect.
 *
 * They are:
 *
 * Enthrall Opposition
 * Laughing Gas
 * Flitterflutter
 *
 * Enthrall Opposition ignores Flitterflutter
 * Laughing Gas ignores Enthrall Opposition
 * Flitterflutter ignores Laughing Gas
 *
 * Additionally, two Laughing Gases cause each other to be ignored.
 *
 * XXX This function is extremely ugly, and a better way should be found.
 */
static void fix_priority_2(card *c, card *d)
{
	/* Cards belonging to the same player are not affected */
	if (c->owner == d->owner) return;

	/* Check for cards already ignored */
	if (c->text_ignored || d->text_ignored) return;

	/* Check for matching cards */
	if (c->d_ptr == d->d_ptr)
	{
		/* Check for Laughing Gas */
		if (!strcmp(c->d_ptr->name, "Laughing Gas"))
		{
			/* Both cards are ignored */
			c->text_ignored = 1;
			d->text_ignored = 1;
		}

		/* Done */
		return;
	}

	/* Check for "Enthrall Opposition" */
	if (!strcmp(c->d_ptr->name, "Enthrall Opposition"))
	{
		/* Check for opposing "Flitterflutter" */
		if (!strcmp(d->d_ptr->name, "Flitterflutter"))
		{
			/* Ignore Flitterflutter */
			d->text_ignored = 1;
		}

		/* Check for opposing "Laughing Gas" */
		else if (!strcmp(d->d_ptr->name, "Laughing Gas"))
		{
			/* Ignore Enthrall Opposition */
			c->text_ignored = 1;
		}
	}

	/* Check for "Flitterflutter" */
	else if (!strcmp(c->d_ptr->name, "Flitterflutter"))
	{
		/* Check for opposing "Laughing Gas" */
		if (!strcmp(d->d_ptr->name, "Laughing Gas"))
		{
			/* Ignore Laughing Gas */
			d->text_ignored = 1;
		}

		/* Check for opposing "Enthrall Opposition" */
		else if (!strcmp(d->d_ptr->name, "Enthrall Opposition"))
		{
			/* Ignore Flitterflutter */
			c->text_ignored = 1;
		}
	}

	/* Check for "Laughing Gas" */
	else if (!strcmp(c->d_ptr->name, "Laughing Gas"))
	{
		/* Check for opposing "Enthrall Opposition" */
		if (!strcmp(d->d_ptr->name, "Enthrall Opposition"))
		{
			/* Ignore Enthrall Opposition */
			d->text_ignored = 1;
		}

		/* Check for opposing "Flitterflutter" */
		if (!strcmp(d->d_ptr->name, "Flitterflutter"))
		{
			/* Ignore Laughing Gas */
			c->text_ignored = 1;
		}
	}
}


/*
 * Notice "category 1" effects on cards.
 *
 * This should be called anytime a card is played, discarded, retrieved,
 * made inactive, etc.
 */
void notice_effect_1(game *g)
{
	int i, j;
	player *p;
	card *c;
	card *list[DECK_SIZE];
	int num = 0;
	int b_p, b_i = 0;

	/* First reset card effects */
	reset_cards(g);
	
	/* Loop over each player */
	for (i = 0; i < 2; i++)
	{
		/* Get player pointer */
		p = &g->p[i];

		/* Loop over cards */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Inactive cards have no effect */
			if (!c->active) continue;

			/* Check correct special effect category */
			if (c->d_ptr->special_cat != 1) continue;

			/* Add card to list */
			list[num++] = c;
		}
	}

	/* Handle all effects */
	while (num)
	{
		/* Reset best priority */
		b_p = 999;
		
		/* Loop over list */
		for (i = 0; i < num; i++)
		{
			/* Check for better priority */
			if (list[i]->d_ptr->special_prio < b_p)
			{
				/* Track best priority and card */
				b_p = list[i]->d_ptr->special_prio;
				b_i = i;
			}
			
			/* XXX XXX XXX Multiple priority 2 cards are weird */
			else if (b_p == 2 && list[i]->d_ptr->special_prio == 2)
			{
				/* Fix cards */
				fix_priority_2(list[i], list[b_i]);
			}
		}

		/* Ensure text is not ignored by previous effects */
		if (!list[b_i]->text_ignored)
		{
			/* Handle effect */
			handle_effect_1(g, list[b_i]->owner, list[b_i]->d_ptr);
		}

		/* Remove card from list */
		list[b_i] = list[--num];
	}
}

/*
 * Check that cards with a phrase like "one of my ..." have a valid
 * target card.  If there are multiple choices, we might ask the player
 * to choose.
 */
static void check_targets(game *g, int who, int ask)
{
	game sim;
	player *p;
	card *c, *t;
	design *list[DECK_SIZE], *b_t = NULL;
	int i, j, num;
	int power, b_p = -1;
	int effect, type, value;
	int changed = 0;

	/* Get player pointer */
	p = &g->p[who];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip non-category 1 effect cards */
		if (c->d_ptr->special_cat != 1) continue;

		/* Skip cards without "one of my" effects */
		if (!(c->d_ptr->special_effect &
		        (S1_ONE_CHAR | S1_ONE_BOOSTER | S1_ONE_SUPPORT)))
		{
			/* Skip */
			continue;
		}

		/* Skip cards that already have a target */
		if (c->target) continue;

		/* Get special effect code and value */
		effect = c->d_ptr->special_effect;
		value = c->d_ptr->special_value;

		/* Clear type mask */
		type = 0;

		/* Create type mask */
		if (effect & S1_ONE_CHAR) type |= TYPE_CHARACTER;
		if (effect & S1_ONE_BOOSTER) type |= TYPE_BOOSTER;
		if (effect & S1_ONE_SUPPORT) type |= TYPE_SUPPORT;

		/* Assume no legal targets */
		num = 0;

		/* Loop over cards */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			t = &p->deck[j];

			/* Skip inactive cards */
			if (!t->active) continue;

			/* Skip cards of wrong type */
			if (!(t->type & type)) continue;

			/* Check for caterpillar qualification */
			if (effect & S1_CATERPILLAR)
			{
				/* Skip non-category 7 cards */
				if (t->d_ptr->special_cat != 7) continue;

				/* Skip non-caterpillar cards */
				if (!(t->d_ptr->special_effect &
				      S7_CATERPILLAR))
				{
					/* Skip card */
					continue;
				}
			}

			/* Check for boosting only earth in fire fight */
			if (g->fight_started && !g->fight_element &&
			    (effect & S1_EARTH_VAL) && !(effect & S1_FIRE_VAL))
			{
				/* Skip card */
				continue;
			}

			/* Check for boosting only fire in earth fight */
			if (g->fight_started && g->fight_element &&
			    (effect & S1_FIRE_VAL) && !(effect & S1_EARTH_VAL))
			{
				/* Skip card */
				continue;
			}

			/* Add card design to list */
			list[num++] = t->d_ptr;
		}

		/* Do nothing if no cards eligible */
		if (!num) continue;

		/* Check for only one card eligible */
		if (num == 1)
		{
			/* Set card target */
			c->target = list[0];

			/* Get target card */
			t = find_card(g, who, list[0]);

			/* Targets have changed */
			changed = 1;

			/* Done */
			continue;
		}

		/* Check for not allowed to ask (yet) */
		if (!ask) continue;

		/* Check for effect affecting total power */
		if (effect & (S1_FIRE_VAL | S1_EARTH_VAL))
		{
			/* Loop over possible targets */
			for (j = 0; j < num; j++)
			{
				/* Simulate game */
				sim = *g;

				/* Set target */
				sim.p[who].deck[i].target = list[j];

				/* Notice effect */
				notice_effect_1(&sim);

				/* Get power */
				power = compute_power(&sim, who);

				/* Check for more power */
				if (power > b_p)
				{
					/* Remember most power */
					b_p = power;
					b_t = list[j];
				}
			}

			/* Set target to that which resulted in most power */
			c->target = b_t;

			/* Consider target changed */
			changed = 1;

			/* Done */
			continue;
		}

		/* Ask user which card to boost */
		p->control->choose(g, who, who, list, num, 1, 1, boost_callback,
		                   c->d_ptr, _("Choose card to boost"));
	}

	/* Notice special text */
	if (changed) notice_effect_1(g);
}

/*
 * Return true if the given card can be retrieved.
 */
int retrieve_legal(game *g, card *c)
{
	player *p, *opp;
	int i;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Inactive cards may not be retrieved */
	if (!c->active) return 0;

	/* Check for no RETRIEVE icon */
	if (!(c->icons & ICON_RETRIEVE)) return 0;

	/* Check for hand limit */
	if (p->stack[LOC_HAND] >= hand_limit(g, g->turn)) return 0;

	/* Check for character */
	if (c->d_ptr->type == TYPE_CHARACTER)
	{
		/* Loop over opponent's cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Skip non-character cards */
			if (c->d_ptr->type != TYPE_CHARACTER) continue;

			/* Check for depicted RETRIEVE icon */
			if (c->d_ptr->icons & ICON_RETRIEVE) return 0;
		}
	}

	/* Card can be retrieved */
	return 1;
}

/*
 * Retrieve the given card back into the current player's hand.
 */
void retrieve_card(game *g, design *d_ptr)
{
	char msg[1024];
	player *p;
	card *c;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s retrieves %s.\n"),
		             _(g->p[g->turn].p_ptr->name), _(d_ptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Current player */
	p = &g->p[g->turn];

	/* Find card */
	c = find_card(g, g->turn, d_ptr);

	/* Cards in hand become inactive */
	deactivate_card(c);

	/* Reduce old stack size */
	p->stack[c->where]--;

	/* Move card into hand */
	c->where = LOC_HAND;

	/* Increase hand size */
	p->stack[LOC_HAND]++;

	/* Card's location in hand is known */
	c->loc_known = 1;

	/* Take notice of affected special texts */
	notice_effect_1(g);
}

/*
 * Return TRUE if two card designs are a PAIR.
 */
int pair_match(design *d, design *e)
{
	int len = 0;
	char *ptr;

	/* Start at beginning of (untranslated!) name */
	ptr = d->name;

	/* Count length of first word */
	while (*ptr && *ptr != ' ')
	{
		/* Count length */
		len++;

		/* Advance pointer */
		ptr++;
	}

	/* Compare first words */
	return !strncmp(d->name, e->name, len);
}

/*
 * Return true if the special text condition of a card matches.
 *
 * This includes mutants and the Buka B.P. cards.
 */
static int card_text_matches(game *g, design *d_ptr)
{
	player *p, *opp;
	card *c;
	int effect, value;
	int type, count, i;

	/* Get our pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Get card in question */
	c = find_card(g, g->turn, d_ptr);

	/* Get mutant's effect code and value */
	effect = d_ptr->special_effect;
	value = d_ptr->special_value;

	/* Check for needing fire element */
	if ((effect & S5_FIRE_POWER) && (g->fight_element != 0))
	{
		/* Not allowed */
		return 0;
	}

	/* Check for needing earth element */
	if ((effect & S5_EARTH_POWER) && (g->fight_element != 1))
	{
		/* Not allowed */
		return 0;
	}

	/* Check for needing total power */
	if (effect & (S5_FIRE_POWER | S5_EARTH_POWER | S5_EITHER_POWER))
	{
		/* Fight must be started */
		if (!g->fight_started) return 0;

		/* Check total power */
		if (compute_power(g, !g->turn) < value) return 0;
	}

	/* Check for opponent active cards */
	if (effect & S5_YOU_ACTIVE)
	{
		/* Clear count */
		count = 0;

		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Count character/booster/support cards */
			if (c->where == LOC_COMBAT ||
			    c->where == LOC_SUPPORT)
			{
				/* Count card */
				count++;
			}
		}

		/* Check for insufficient active cards */
		if (count < value) return 0;
	}

	/* Check for our active cards */
	if (effect & S5_MY_PLAYED)
	{
		/* Check for too many played cards */
		if (p->stack[LOC_COMBAT] +
		    p->stack[LOC_SUPPORT] > value) return 0;
	}

	/* Check for my influence cards */
	if (effect & S5_MY_INFLUENCE)
	{
		/* Clear count */
		count = 0;

		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Skip non-influence cards */
			if (c->d_ptr->type != TYPE_INFLUENCE) continue;

			/* Landed ships aren't really in influence area */
			if (c->landed) continue;

			/* Count influence cards */
			count++;
		}

		/* Check for too many influence cards */
		if (count > value) return 0;
	}

	/* Check for opponent active cards */
	if (effect & (S5_YOU_CHARACTER | S5_YOU_BOOSTER | S5_YOU_SUPPORT))
	{
		/* Clear count */
		count = 0;

		/* Clear type mask */
		type = 0;

		/* Set type mask */
		if (effect & S5_YOU_CHARACTER) type |= TYPE_CHARACTER;
		if (effect & S5_YOU_BOOSTER) type |= TYPE_BOOSTER;
		if (effect & S5_YOU_SUPPORT) type |= TYPE_SUPPORT;

		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Check for correct type */
			if (c->type & type) count++;
		}

		/* Check for not enough opponent cards */
		if (count < value) return 0;
	}

	/* Check for opponent cards with icons */
	if (effect & S5_YOU_ICONS)
	{
		/* Clear count */
		count = 0;

		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Check for printed icons */
			if (c->d_ptr->icons) count++;
		}

		/* Check for not enough opponent icons */
		if (count < value) return 0;
	}

	/* Check for needing dragons */
	if (effect & S5_YOU_DRAGONS)
	{
		/* Check for opponent having too few dragons */
		if (opp->dragons < value) return 0;
	}

	/* Check for your played cards */
	if (effect & S5_YOU_PLAYED)
	{
		/* Check for too few played cards */
		if (opp->stack[LOC_COMBAT] +
		    opp->stack[LOC_SUPPORT] < value) return 0;
	}

	/* Compare handsizes */
	if (effect & S5_YOU_HANDSIZE)
	{
		/* Check for too many cards in hand */
		if (p->stack[LOC_HAND] + value >
		    opp->stack[LOC_HAND]) return 0;
	}

	/* Card matches */
	return 1;
}

/*
 * Return true if a given card design can be played.
 *
 * "Category 3" special effects affect this.
 *
 * FREE, PAIR, and GANG icons affect this.
 */
int card_allowed(game *g, design *d_ptr)
{
	player *p, *opp;
	card *c;
	design *pair_list[DECK_SIZE];
	int num_pair = 0, match;
	int gang_good = ICON_GANG_MASK;
	int effect, value;
	int played_support, played_booster, played_all;
	int real_support, real_booster, real_char;
	int max_support, max_booster, max_either;
	int stop_played = 0, temp_free = 0;
	int i, j;

	/* Get our pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Clear counts of played cards */
	played_support = real_support = 0;
	played_booster = real_booster = 0;
	real_char = 0;
	played_all = 0;

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not played this turn */
		if (!c->recent) continue;

		/* Count card */
		played_all++;

		/* Check for cards loaded onto ship */
		if (c->where == LOC_INFLUENCE &&
		    c->type != TYPE_INFLUENCE)
		{
			/* Count as played support */
			played_support++;

			/* Done */
			continue;
		}

		/* Check for character */
		if (c->type == TYPE_CHARACTER) real_char++;

		/* Check for support */
		if (c->type == TYPE_SUPPORT) real_support++;

		/* Check for booster */
		if (c->type == TYPE_BOOSTER) real_booster++;

		/* Cards with STOP icons prevent further cards */
		if (c->icons & ICON_STOP) stop_played = 1;

		/* Don't count cards with FREE icon */
		if (c->icons & ICON_FREE) continue;

		/* Don't count cards that were played as FREE */
		if (c->was_played_free) continue;

		/* Check for PAIR icon */
		if (c->icons & ICON_PAIR)
		{
			/* Assume no pair match */
			match = 0;

			/* Attempt to find matching pair */
			for (j = 0; j < num_pair; j++)
			{
				/* Check for match */
				if (pair_match(c->d_ptr, pair_list[j]))
				{
					/* Remove pair from list */
					pair_list[j] = pair_list[--num_pair];

					/* Pair was matched */
					match = 1;

					/* Done */
					break;
				}
			}

			/* Check for match found */
			if (match) continue;

			/* Add design to pair list */
			pair_list[num_pair++] = c->d_ptr;
		}

		/* Check for combat cards */
		if (c->where == LOC_COMBAT)
		{
			/* Track which gang is legal to play */
			gang_good &= (c->icons & ICON_GANG_MASK);
		}

		/* Check for support */
		if (c->type == TYPE_SUPPORT) played_support++;

		/* Check for booster */
		if (c->type == TYPE_BOOSTER) played_booster++;
	}

	/* Check for stop icon */
	if (stop_played)
	{
		/* Count FREE cards among number played */
		played_support = real_support;
		played_booster = real_booster;
	}

	/* Check for a card played */
	if (played_all)
	{
		/* Leadership phase cards can no longer be played */
		if (d_ptr->type == TYPE_LEADERSHIP ||
		    d_ptr->type == TYPE_INFLUENCE)
		{
			/* Cannot be played */
			return 0;
		}
	}

	/* Loop over opponent cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with ignored text */
		if (c->text_ignored) continue;

		/* Skip cards that do not have "category 3" effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Get effect code and value */
		effect = c->d_ptr->special_effect;
		value = c->d_ptr->special_value;

		/* Skip cards that aren't "you may not" */
		if (!(effect & S3_YOU_MAY_NOT)) continue;

		/* Check for "may not play more than" effect */
		if (effect & S3_MORE_THAN)
		{
			/* Check for combat card flag */
			if (effect & S3_COMBAT)
			{
				/* Check for too many combat cards */
				if (real_char + real_booster >= value)
				{
					/* Disallow characters and boosters */
					if (d_ptr->type == TYPE_CHARACTER ||
					    d_ptr->type == TYPE_BOOSTER)
					{
						/* Disallow */
						return 0;
					}
				}
			}

			/* Check for character flag */
			else if (effect & S3_CHARACTER)
			{
				/* Disallow multiple characters */
				if (real_char > value) return 0;
			}

			/* Otherwise check all cards played */
			else if (played_all >= value) return 0;
		}

		/* Check for type match */
		else if
		   ((d_ptr->type == TYPE_SUPPORT && (effect & S3_SUPPORT)) ||
		   (d_ptr->type == TYPE_BOOSTER && (effect & S3_BOOSTER)) ||
		   (d_ptr->type == TYPE_CHARACTER && (effect & S3_CHARACTER)) ||
		   (d_ptr->type == TYPE_LEADERSHIP && (effect & S3_LEADERSHIP)))
		{
			/* Check for "has special text" flag */
			if (effect & S3_HAVE_SPECIAL)
			{
				/* Check for text */
				if (d_ptr->text) return 0;
			}

			/* Check for "no special text flag" */
			else if (effect & S3_NO_SPECIAL)
			{
				/* Check for no text */
				if (!d_ptr->text) return 0;
			}

			/* Check for "with value higher than" flag */
			else if (effect & S3_WITH_VALUE)
			{
				/* Check for value too high */
				if (d_ptr->value[0] > value) return 0;
				if (d_ptr->value[1] > value) return 0;
			}

			/* Otherwise not allowed at all */
			else return 0;
		}
	}

	/* Normal maximum amounts */
	max_support = max_booster = 0;
	max_either = 1;

	/* Check for beginning of fight */
	if (!g->fight_started) max_either = 0;

	/* Check for STOP icon */
	if (stop_played) max_either = 0;

	/* Check our cards for "I may play" effects */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with ignored text */
		if (c->text_ignored) continue;

		/* Skip cards that do not have "category 3" effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Get effect code and value */
		effect = c->d_ptr->special_effect;
		value = c->d_ptr->special_value;

		/* Skip cards that aren't "I may" */
		if (!(effect & S3_I_MAY_PLAY)) continue;

		/* Check for support flag */
		if (effect & S3_SUPPORT)
		{
			/* Check for additional */
			if (effect & S3_ADDITIONAL) max_support += value;

			/* Set value */
			else max_support = value;
		}

		/* Check for booster flag */
		if (effect & S3_BOOSTER)
		{
			/* Check for additional */
			if (effect & S3_ADDITIONAL) max_booster += value;
			
			/* Set value */
			else max_booster = value;
		}

		/* Check for unused "as free" flag */
		if ((effect & S3_AS_FREE) && !c->used)
		{
			/* Set temporary free flag */
			temp_free = 1;
		}
	}

	/* Find card we are attempting to play */
	c = find_card(g, g->turn, d_ptr);

	/* Check for "play free if" flag */
	if (!c->text_ignored && d_ptr->special_cat == 5 &&
	    (d_ptr->special_effect & S5_PLAY_FREE_IF) &&
	    card_text_matches(g, d_ptr))
	{
		/* Play as free */
		temp_free = 1;
	}

	/* Check for Buka character played from ship */
	if (c->ship && d_ptr->people == 8 && d_ptr->type == TYPE_CHARACTER)
	{
		/* Play as free */
		temp_free = 1;
	}

	/* Set temporary free if needed */
	if (temp_free && !stop_played)
	{
		/* Set temp free on card */
		c->playing_free = 1;

		/* Notice special text */
		notice_effect_1(g);
	}

	/* Check for usable FREE icon */
	if (((c->icons & ICON_FREE) || c->playing_free) && !stop_played)
	{
		/* Check for temporary free */
		if (temp_free)
		{
			/* Clear temp free */
			c->playing_free = 0;

			/* Notice special text */
			notice_effect_1(g);
		}

		/* Check for text on card disallowing play */
		if (!c->text_ignored && d_ptr->special_cat == 5 &&
		    (c->d_ptr->special_effect & S5_PLAY_ONLY_IF) &&
		    !card_text_matches(g, d_ptr)) return 0;

		/* FREE makes card allowed, regardless of count */
		return 1;
	}

	/* Check for PAIR icon */
	if ((c->icons & ICON_PAIR) && !stop_played)
	{
		/* Loop over active pairs */
		for (i = 0; i < num_pair; i++)
		{
			/* Check for match */
			if (pair_match(d_ptr, pair_list[i])) return 1;
		}
	}

	/* Check for GANG icon */
	if ((c->icons & ICON_GANG_MASK) && !stop_played)
	{
		/* Check for match with existing gang (if any) */
		if ((c->icons & ICON_GANG_MASK) & gang_good) return 1;
	}

	/* Account for booster cards played */
	for (i = 0; i < played_booster; i++)
	{
		/* Reduce max boosters if possible */
		if (max_booster) max_booster--;

		/* Otherwise reduce booster/support */
		else if (max_either) max_either--;
	}

	/* Account for support cards played */
	for (i = 0; i < played_support; i++)
	{
		/* Reduce max support if possible */
		if (max_support) max_support--;

		/* Otherwise reduce booster/support */
		else if (max_either) max_either--;
	}

	/* Check for booster */
	if (d_ptr->type == TYPE_BOOSTER)
	{
		/* Check for allowed boosters */
		if (max_booster || max_either) return 1;

		/* Otherwise not allowed */
		return 0;
	}

	/* Check for support */
	if (d_ptr->type == TYPE_SUPPORT)
	{
		/* Check for allowed support */
		if (max_support || max_either) return 1;

		/* Otherwise not allowed */
		return 0;
	}

	/* Check for character */
	if (d_ptr->type == TYPE_CHARACTER)
	{
		/* Check for character already played */
		if (p->char_played) return 0;
	}

	/* Check for restriction on card */
	if (!c->text_ignored && d_ptr->special_cat == 5 &&
	    (d_ptr->special_effect & S5_PLAY_ONLY_IF) &&
	    !card_text_matches(g, d_ptr)) return 0;

	/* Card is allowed */
	return 1;
}

/*
 * Return true if the given card is eligible to be played.
 *
 * Normally this means "in the hand", but if a ship is landed, the card
 * needs to be on a landed ship.  Of course there are special power texts
 * that cause exceptions.
 */
int card_eligible(game *g, design *d_ptr)
{
	player *p;
	card *c, *playing;
	int ship = 0, ship_hand = 0, on_ship = 0;
	int i;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Find card we intend to play */
	playing = find_card(g, g->turn, d_ptr);

	/* Non-character/booster/support cards can be played if in hand */
	if (d_ptr->type > TYPE_SUPPORT)
	{
		/* Card is eligible if in the hand */
		return playing->where == LOC_HAND;
	}

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip unlanded ships */
		if (!c->landed) continue;

		/* There are landed ships */
		ship = 1;

		/* Check for "category 3" special text on ship */
		if (!c->text_ignored && c->d_ptr->special_cat == 3)
		{
			/* Check for "ship/hand" code */
			if (c->d_ptr->special_effect & S3_SHIP_HAND)
			{
				/* Cards may be played from the hand */
				ship_hand = 1;
			}
		}

		/* Check for card on landed ship */
		if (playing->ship == c->d_ptr) on_ship = 1;
	}

	/* Check for no landed ships */
	if (!ship)
	{
		/* Card is eligible if in the hand */
		return playing->where == LOC_HAND;
	}

	/* Check for on a landed ship */
	if (on_ship) return 1;

	/* Check for in hand, but cards from hand are eligible */
	if (playing->where == LOC_HAND && ship_hand) return 1;

	/* Card is not eligible */
	return 0;
}

/*
 * Return true if we can play an additional "support" card.
 *
 * This is used for loading ships and playing bluff cards.
 */
int support_allowed(game *g)
{
	player *p, *opp;
	card *c;
	int played_support, played_booster, played_all;
	int real_support, real_booster;
	int max_support, max_booster, max_either;
	int stop_played = 0;
	int effect, value;
	int i;

	/* Get our pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Clear count of played cards */
	played_support = real_support = 0;
	played_booster = real_booster = 0;
	played_all = 0;

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not played this turn */
		if (!c->recent) continue;

		/* Count cards played */
		played_all++;

		/* Check for cards loaded onto ship */
		if (c->where == LOC_INFLUENCE &&
		    c->type != TYPE_INFLUENCE)
		{
			/* Count as played support */
			played_support++;
			real_support++;

			/* Done */
			continue;
		}

		/* Check for support */
		if (c->type == TYPE_SUPPORT) real_support++;

		/* Check for booster */
		if (c->type == TYPE_BOOSTER) real_booster++;

		/* Cards with STOP icons prevent further cards */
		if (c->icons & ICON_STOP) stop_played = 1;

		/* Don't count cards with FREE icon */
		if (c->icons & ICON_FREE) continue;

		/* Don't count cards that were played as FREE */
		if (c->was_played_free) continue;

		/* Check for support */
		if (c->type == TYPE_SUPPORT) played_support++;

		/* Check for booster */
		if (c->type == TYPE_BOOSTER) played_booster++;
	}

	/* Check for STOP icon */
	if (stop_played)
	{
		/* Count FREE cards among number played */
		played_support = real_support;
		played_booster = real_booster;
	}

	/* Loop over opponent cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with ignored text */
		if (c->text_ignored) continue;

		/* Skip cards that do not have "category 3" effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Get effect code and value */
		effect = c->d_ptr->special_effect;
		value = c->d_ptr->special_value;

		/* Skip cards that aren't "you may not" */
		if (!(effect & S3_YOU_MAY_NOT)) continue;

		/* Check for restriction on support cards */
		if (effect & S3_SUPPORT) return 0;

		/* Check for "may not play more than" effect */
		if (effect & S3_MORE_THAN)
		{
			/* Check for effects that don't affect support */
			if (effect & S3_COMBAT) continue;
			if (effect & S3_CHARACTER) continue;

			/* Check for too many cards played */
			if (played_all >= value) return 0;
		}
	}

	/* Normal maximum amounts */
	max_support = max_booster = 0;
	max_either = 1;

	/* Check for beginning of fight */
	if (!g->fight_started) max_either = 0;

	/* Check for STOP icon */
	if (stop_played) max_either = 0;

	/* Check our cards for "I may play" effects */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with ignored text */
		if (c->text_ignored) continue;

		/* Skip cards that do not have "category 3" effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Get effect code and value */
		effect = c->d_ptr->special_effect;
		value = c->d_ptr->special_value;

		/* Skip cards that aren't "I may" */
		if (!(effect & S3_I_MAY_PLAY)) continue;

		/* Check for support flag */
		if (effect & S3_SUPPORT)
		{
			/* Check for additional */
			if (effect & S3_ADDITIONAL) max_support += value;

			/* Set value */
			else max_support = value;
		}

		/* Check for booster flag */
		if (effect & S3_BOOSTER)
		{
			/* Check for additional */
			if (effect & S3_ADDITIONAL) max_booster += value;
			
			/* Set value */
			else max_booster = value;
		}
	}

	/* Account for booster cards played */
	for (i = 0; i < played_booster; i++)
	{
		/* Reduce max boosters if possible */
		if (max_booster) max_booster--;

		/* Otherwise reduce booster/support */
		else if (max_either) max_either--;
	}

	/* Account for support cards played */
	for (i = 0; i < played_support; i++)
	{
		/* Reduce max support if possible */
		if (max_support) max_support--;

		/* Otherwise reduce booster/support */
		else if (max_either) max_either--;
	}

	/* Check if support cards can be played */
	if (max_support > 0 || max_either > 0) return 1;

	/* Card cannot be played */
	return 0;
}

/*
 * Return true if the given ship is full.
 */
static int ship_full(game *g, int who, design *d_ptr)
{
	player *p;
	card *c;
	int i, count = 0;

	/* Get player pointer */
	p = &g->p[who];

	/* Loop over deck */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Check for on ship */
		if (c->ship == d_ptr) count++;
	}

	/* Check for full */
	if (count >= d_ptr->capacity) return 1;

	/* Still room */
	return 0;
}

/*
 * Return true if the given ship can have a card loaded on it.
 */
int load_allowed(game *g, design *d_ptr)
{
	player *p;
	card *c;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Check for support card play allowed */
	if (!support_allowed(g)) return 0;

	/* Find ship */
	c = find_card(g, g->turn, d_ptr);

	/* Cards cannot be loaded onto landed ship */
	if (c->landed) return 0;

	/* Check for full ship */
	if (ship_full(g, g->turn, d_ptr)) return 0;

	/* Assume load is allowed */
	return 1;
}

/*
 * Discard a random bluff card from the given player.
 *
 * If game is simulated, mark card accordingly.
 */
static void discard_bluff(game *g, int who)
{
	player *p;
	card *c, *chosen = NULL;
	int i, n = 0, faceup = 1;

	/* Get player pointer */
	p = &g->p[who];

	/* Loop over deck */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip non-bluff cards */
		if (!c->bluff) continue;

		/* Pick randomly */
		if ((myrand(&g->random_seed) % (++n)) == 0) chosen = c;
	}

	/* Check for simulation */
	if (g->simulation)
	{
		/* Mark chosen card as random */
		chosen->random_fake = 1;

		/* Do not move card face-up */
		faceup = 0;
	}

	/* Clear bluff flag */
	chosen->bluff = 0;

	/* Move to discard */
	move_card(g, who, chosen->d_ptr, LOC_DISCARD, faceup);
}

/*
 * Discard a random non-disclosed card from a player's hand.
 */
static void discard_random(game *g, int who)
{
	player *p;
	card *c, *chosen = NULL;
	int i, n = 0, dest;

	/* Get player pointer */
	p = &g->p[who];

	/* Loop over deck */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not in hand */
		if (c->where != LOC_HAND) continue;

		/* Skip disclosed cards */
		if (c->disclosed) continue;

		/* Pick randomly */
		if ((myrand(&g->random_seed) % (++n)) == 0) chosen = c;
	}

	/* Check for simulation */
	if (g->simulation)
	{
		/* Mark chosen card as random */
		chosen->random_fake = 1;
	}

	/* Assume destination is discard pile */
	dest = LOC_DISCARD;

	/* Leadership cards go to leadership pile */
	if (chosen->type == TYPE_LEADERSHIP) dest = LOC_LEADERSHIP;

	/* Move to discard */
	move_card(g, who, chosen->d_ptr, dest, 1);
}

/*
 * Handle a discard choice.
 */
static int discard_callback(game *g, int who, design **list, int num,
                            void *data)
{
	design *d_ptr = (design *)data;
	player *p;
	card *c;
	int effect, type = 0;
	int i, dest, count = 0;

	/* Get special effect */
	effect = d_ptr->special_effect;

	/* Check for "not last character" flag */
	if (effect & S4_NOT_LAST_CHAR)
	{
		/* Get player pointer */
		p = &g->p[who];

		/* Loop over deck */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip non-active cards */
			if (!c->active) continue;

			/* Skip non-characters */
			if (c->type != TYPE_CHARACTER) continue;

			/* Count active characters */
			count++;
		}

		/* Loop over cards chosen */
		for (i = 0; i < num; i++)
		{
			/* Skip discarded bluff cards */
			if (!list[i]) continue;

			/* Find card */
			c = find_card(g, who, list[i]);

			/* Check for character */
			if (c->type == TYPE_CHARACTER)
			{
				/* Count characters discarded */
				count--;
			}
		}

		/* Check for all active characters discarded */
		if (!count) return 0;
	}

	/* Check for "either" flag */
	if (effect & S4_EITHER)
	{
		/* Loop over cards chosen */
		for (i = 0; i < num; i++)
		{
			/* Handle discarded bluff cards */
			if (!list[i])
			{
				/* Check for no type yet */
				if (!type)
				{
					/* Set type to support */
					type = TYPE_SUPPORT;

					/* Next card */
					continue;
				}
				
				/* Check for mismatch */
				if (type != TYPE_SUPPORT) return 0;

				/* Next card */
				continue;
			}

			/* Get card */
			c = find_card(g, who, list[i]);

			/* Check for no type yet */
			if (!type)
			{
				/* Set type to first in list */
				type = c->type;

				/* Next */
				continue;
			}

			/* Check for mismatch */
			if (type != c->type) return 0;
		}

		/* Check for "ALL" flag as well */
		if (effect & S4_ALL)
		{
			/* Get player pointer */
			p = &g->p[who];

			/* Loop over deck */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip non-active cards */
				if (!c->active) continue;

				/* Skip non-matching type */
				if (c->type != type) continue;

				/* Skip protected cards */
				if (c->icons & ICON_PROTECTED) continue;

				/* Count cards of type */
				count++;
			}

			/* Check for not all discarded */
			if (count > num) return 0;
		}
	}

	/* Loop over cards */
	for (i = 0; i < num; i++)
	{
		/* Check for bluff card */
		if (!list[i])
		{
			/* Check for discard from hand */
			if (effect & S4_YOUR_HAND)
			{
				/* Discard random card from hand */
				discard_random(g, who);

				/* Next card */
				continue;
			}

			/* Discard a random bluff card */
			discard_bluff(g, who);

			/* Next */
			continue;
		}

		/* Check for leadership card */
		if (list[i]->type == TYPE_LEADERSHIP)
		{
			/* Discarded leadership cards go in seperate pile */
			dest = LOC_LEADERSHIP;
		}
		else
		{
			/* Everything else goes in discard pile */
			dest = LOC_DISCARD;
		}

		/* Discard */
		move_card(g, who, list[i], dest, 1);
	}

	/* Notice special effects */
	notice_effect_1(g);

	/* Discarding from hand is not critical */
	if (effect & S4_MY_HAND) return 2;

	/* Success */
	return 1;
}

/*
 * Handle a retrieve choice.
 */
static int retrieve_callback(game *g, int who, design **list, int num,
                             void *data)
{
	int i;

	/* Loop over cards */
	for (i = 0; i < num; i++)
	{
		/* Retrieve */
		retrieve_card(g, list[i]);
	}

	/* Success */
	return 1;
}

/*
 * Handle an "undraw" choice.  We put the given cards back in the draw pile.
 */
static int undraw_callback(game *g, int who, design **list, int num, void *data)
{
	design *d_ptr = (design *)data;
	card *c;
	int effect;
	int i;

	/* Get card effect */
	effect = d_ptr->special_effect;

	/* Loop over cards */
	for (i = 0; i < num; i++)
	{
		/* Put in draw pile */
		move_card(g, who, list[i], LOC_DRAW, 0);

		/* Check for "on bottom" flag */
		if (effect & S4_ON_BOTTOM)
		{
			/* Get card pointer */
			c = find_card(g, who, list[i]);

			/* Set bottom flag */
			c->on_bottom = 1;
		}
	}

	/* Success */
	return 2;
}

/*
 * Handle an "draw" choice.  We take cards into our hand.
 */
static int draw_callback(game *g, int who, design **list, int num, void *data)
{
	design *d_ptr = (design *)data;
	player *p, *opp;
	card *c;
	int effect, value;
	int i;

	/* Get player pointer */
	p = &g->p[who];

	/* Get opponent pointer */
	opp = &g->p[!who];

	/* Check for taking from draw deck */
	if (!(d_ptr->special_effect & S4_MY_DISCARD))
	{
		/* Look for "you may not draw" */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Skip cards with text ignored */
			if (c->text_ignored) continue;

			/* Skip non-category 3 effects */
			if (c->d_ptr->special_cat != 3) continue;

			/* Get effect code and value */
			effect = c->d_ptr->special_effect;
			value = c->d_ptr->special_value;

			/* Skip effects that aren't "you may not draw" */
			if (effect != (S3_YOU_MAY_NOT | S3_DRAW)) continue;

			/* Check for too many cards drawn */
			if (p->cards_drawn + num > value) return 0;
		}
	}

	/* Check hand limit */
	if (p->stack[LOC_HAND] + num > hand_limit(g, who)) return 0;

	/* Loop over cards */
	for (i = 0; i < num; i++)
	{
		/* Take into hand */
		move_card(g, who, list[i], LOC_HAND,
		          d_ptr->special_effect & S4_REVEAL);

		/* Count cards drawn */
		p->cards_drawn++;
	}

	/* Handle category 1 effects */
	notice_effect_1(g);

	/* Success */
	return 1;
}

/*
 * Handle a "load" choice.
 */
static int load_callback(game *g, int who, design **list, int num, void *data)
{
	design *d_ptr = (design *)data;
	player *p;
	card *c;
	int i;

	/* Get player pointer */
	p = &g->p[who];

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Load card */
		load_card(g, list[i], d_ptr);

		/* Find card */
		c = find_card(g, g->turn, list[i]);

		/* Clear "recent" flag */
		c->recent = 0;
	}

	/* Success */
	return 2;
}

/*
 * Handle the second part of a "favor" choice.
 */
static int favor2_callback(game *g, int who, design **list, int num, void *data)
{
	design *d_ptr = (design *)data;
	player *p;
	card *c;

	/* Get player pointer */
	p = &g->p[who];

	/* Only one choice allowed */
	if (num != 1) return 0;

	/* Load card */
	load_card(g, d_ptr, list[0]);

	/* Find card loaded */
	c = find_card(g, g->turn, d_ptr);

	/* Clear "recent" flag */
	c->recent = 0;

	/* Choice is valid */
	return 1;
}


/*
 * Handle a "favor" choice.
 */
static int favor_callback(game *g, int who, design **list, int num, void *data)
{
	design *choices[DECK_SIZE];
	player *p, *opp;
	card *c, *d;
	int effect, value;
	int i, j, num_choices = 0, landed = 0;
	char prompt[1024];

	/* Get player pointer */
	p = &g->p[who];

	/* Get opponent pointer */
	opp = &g->p[!who];

	/* Look for "you may not draw" */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with text ignored */
		if (c->text_ignored) continue;

		/* Skip non-category 3 effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Get effect code and value */
		effect = c->d_ptr->special_effect;
		value = c->d_ptr->special_value;

		/* Skip effects that aren't "you may not draw" */
		if (effect != (S3_YOU_MAY_NOT | S3_DRAW)) continue;

		/* Check for too many cards drawn */
		if (p->cards_drawn + num > value) return 0;
	}

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Find card */
		c = find_card(g, who, list[i]);

		/* Check for influence card */
		if (c->type == TYPE_INFLUENCE)
		{
			/* Play it */
			play_card(g, c->d_ptr, 0, 0);
		}
		else
		{
			/* Loop over cards */
			for (j = 1; j < DECK_SIZE; j++)
			{
				/* Get card pointer */
				d = &p->deck[j];

				/* Skip inactive cards */
				if (!d->active) continue;

				/* Skip non-ships */
				if (!d->d_ptr->capacity) continue;

				/* Skip full ships */
				if (ship_full(g, g->turn, d->d_ptr)) continue;

				/* Only give one landed ship choice */
				if (d->landed)
				{
					/* Check for previous landed choice */
					if (landed) continue;

					/* Set flag */
					landed = 1;
				}

				/* Add ship to list */
				choices[num_choices++] = d->d_ptr;
			}

			/* Prompt */
			printf(prompt, _("Choose ship to load"));

			/* Choose cards and load them */
			p->control->choose(g, who, who, choices, num_choices,
			                   1, 1, favor2_callback, c->d_ptr,
			                   prompt);
		}

		/* Count cards drawn */
		p->cards_drawn++;
	}

	/* Choice is legal */
	return 1;
}

/*
 * Handle a "category 4" special effect.
 *
 * These involve moving cards from one pile to another.
 */
static void handle_effect_4(game *g, card *c_ptr, int time)
{
	player *p, *opp;
	design *pick, *list[DECK_SIZE];
	card *c;
	int num_choices = 0, num_bluff = 0;
	int effect, value;
	int type = 0, ships = 0, src, dest, min, max = 0, num_char = 0;
	char prompt[1024];
	int i;

	/* Check for correct timing */
	if (time != c_ptr->d_ptr->special_time) return;

	/* Get player and opponent pointers */
	p = &g->p[g->turn];
	opp = &g->p[!g->turn];

	/* Get effect code and value */
	effect = c_ptr->d_ptr->special_effect;
	value = c_ptr->d_ptr->special_value;

	/* Check for discard effect */
	if (effect & S4_DISCARD)
	{
		/* Check for discard from hand or deck */
		if (effect & (S4_YOUR_HAND | S4_YOUR_DECK))
		{
			/* Loop over opponent cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &opp->deck[i];

				/* Skip cards not in hand */
				if (c->where != LOC_HAND) continue;

				/* Skip undisclosed cards */
				if (!c->disclosed) continue;

				/* Add design to list */
				list[num_choices++] = c->d_ptr;
			}

			/* Check for no disclosed cards or discard from deck */
			if (!num_choices || (effect & S4_YOUR_DECK))
			{
				/* Assume hand */
				src = LOC_HAND;

				/* Check for discard from deck */
				if (effect & S4_YOUR_DECK) src = LOC_DRAW;

				/* Move cards */
				for (i = 0; i < value; i++)
				{
					/* Pick card at random */
					pick = random_card(g, !g->turn, src);

					/* Check for none left */
					if (!pick) break;

					/* Check for leadership */
					if (pick->type == TYPE_LEADERSHIP)
					{
						/* Go to leadership pile */
						dest = LOC_LEADERSHIP;
					}
					else
					{
						/* Else goes to discard */
						dest = LOC_DISCARD;
					}

					/* Move card to discard */
					move_card(g, !g->turn, pick, dest, 1);
				}

				/* Done */
				return;
			}

			/* Add undisclosed cards to list */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &opp->deck[i];

				/* Skip cards not in hand */
				if (c->where != LOC_HAND) continue;

				/* Skip disclosed cards */
				if (c->disclosed) continue;

				/* Add empty card to list */
				list[num_choices++] = NULL;
			}

			/* Check for too few choices */
			if (value > num_choices) value = num_choices;

			/* Create prompt */
			printf(prompt, ngettext("Choose card to discard",
			                         "Choose cards to discard",
			                         value));

			/* Discard */
			p->control->choose(g, g->turn, !g->turn, list,
			                   num_choices, 0, value,
			                   discard_callback, c_ptr->d_ptr,
			                   prompt);
		}

		/* Check for discard of active cards */
		if (effect & (S4_YOUR_CHAR | S4_YOUR_SUPPORT | S4_YOUR_BOOSTER))
		{
			/* Create type */
			if (effect & S4_YOUR_CHAR) type |= TYPE_CHARACTER;
			if (effect & S4_YOUR_BOOSTER) type |= TYPE_BOOSTER;
			if (effect & S4_YOUR_SUPPORT) type |= TYPE_SUPPORT;

			/* Loop over opponent cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &opp->deck[i];

				/* Skip inactive cards */
				if (!c->active) continue;

				/* Skip non-characters */
				if (c->type != TYPE_CHARACTER) continue;

				/* Count characters */
				num_char++;
			}

			/* Check for forbidden to discard last character */
			if ((effect & S4_NOT_LAST_CHAR) && num_char == 1)
			{
				/* Remove character from type mask */
				type &= ~TYPE_CHARACTER;
			}

			/* Loop over opponent cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &opp->deck[i];

				/* Skip cards not in play */
				if (c->where != LOC_COMBAT &&
				    c->where != LOC_SUPPORT) continue;

				/* Check for active requirement */
				if ((effect & S4_ACTIVE) &&
				    !c->active) continue;

				/* Skip cards of wrong type */
				if (!(c->type & type)) continue;

				/* Skip active cards with PROTECTED */
				if (c->active &&
				    (c->icons & ICON_PROTECTED)) continue;

				/* Check for "with icon" restriction */
				if (effect & S4_WITH_ICON)
				{
					/* Check for no icons depicted */
					if (!c->d_ptr->icons) continue;
				}

				/* XXX Check for opponent "you may not" */
				if (c->d_ptr->special_cat == 3 &&
				    (c->d_ptr->special_effect & S3_YOU_MAY_NOT))
				{
					/* XXX May allow different card play */
					p->last_played = 0;
				}

				/* Check for bluff card */
				if (c->bluff)
				{
					/* Count bluff cards to add to list */
					num_bluff++;
				}
				else
				{
					/* Add design to list */
					list[num_choices++] = c->d_ptr;
				}
			}

			/* Add bluff cards to end of list */
			for (i = 0; i < num_bluff; i++)
			{
				/* Add empty card to list */
				list[num_choices++] = NULL;
			}

			/* Restrict maximum value to choices */
			if (value > num_choices) value = num_choices;

			/* Check for no discards allowed */
			if (!value || !num_choices) return;

			/* Assume no minimum */
			min = 0;

			/* Check for "ALL" flag (without "EITHER" flag) */
			if (effect & S4_ALL &&
			    !(effect & S4_EITHER))
			{
				/* Must discard all possible cards */
				min = value;

				/* Check for not last character */
				if (effect & S4_NOT_LAST_CHAR) min--;
			}

			/* Disallow using "on my turn" text without choosing */
			if (time == TIME_MYTURN) min = 1;

			/* Create prompt */
			printf(prompt, ngettext("Choose card to discard",
			                         "Choose cards to discard",
			                         value));

			/* Discard */
			p->control->choose(g, g->turn, !g->turn, list,
			                   num_choices, min, value,
			                   discard_callback, c_ptr->d_ptr,
			                   prompt);
		}

		/* Check for discarding from our hand */
		if (effect & S4_MY_HAND)
		{
			/* Loop over cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip cards not in hand */
				if (c->where != LOC_HAND) continue;

				/* Add design to list */
				list[num_choices++] = c->d_ptr;
			}

			/* Check for no choices */
			if (!num_choices) return;

			/* Create prompt */
			printf(prompt, ngettext("Choose card to discard",
			                         "Choose cards to discard",
						 value));

			/* Discard */
			p->control->choose(g, g->turn, g->turn, list,
			                   num_choices, 0, value,
			                   discard_callback, c_ptr->d_ptr,
			                   prompt);
		}

		/* Done */
		return;
	}

	/* Check for drawing cards */
	if (effect & S4_DRAW)
	{
		/* XXX May allow different card play */
		p->last_played = 0;

		/* Check for "draw to" */
		if (effect & S4_TO)
		{
			/* Reset value to number of cards to draw */
			value = value - p->stack[LOC_HAND];

			/* Do nothing if too many cards */
			if (value < 0) value = 0;
		}

		/* Check for drawing from discard pile */
		if (effect & S4_MY_DISCARD)
		{
			/* Loop over cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip cards not in discard */
				if (c->where != LOC_DISCARD) continue;

				/* Clear location known flag */
				if (!(effect & S4_REVEAL)) c->loc_known = 0;
			}
		}

		/* Restrict amount to draw due to hand limit */
		max = hand_limit(g, g->turn) - p->stack[LOC_HAND];

		/* Check for hand limit restriction */
		if (value > max) value = max;

		/* Draw cards */
		for (i = 0; i < value; i++)
		{
			/* Check for not drawing from draw pile */
			if (effect & S4_MY_DISCARD)
			{
				/* Choose card from discard pile */
				pick = random_card(g, g->turn, LOC_DISCARD);
			}
			else
			{
				/* Choose card from draw pile */
				pick = random_draw(g, g->turn);
			}

			/* Check for no drawing allowed */
			if (!pick) continue;

			/* Move card to hand */
			move_card(g, g->turn, pick, LOC_HAND,
			          effect & S4_REVEAL);

			/* Add card to list */
			list[num_choices++] = pick;
		}

		/* Handle category 1 effects */
		notice_effect_1(g);

		/* Check for discard one of cards just drawn */
		if (num_choices >= 1 && (effect & S4_DISCARD_ONE))
		{
			/* Create prompt */
			printf(prompt, _("Choose drawn card to discard"));

			/* Prompt for card to discard */
			p->control->choose(g, g->turn, g->turn, list,
			                   num_choices, 1, 1,
			                   discard_callback, c_ptr->d_ptr,
			                   prompt);
		}

		/* Check for "UNDRAW_2" */
		if (effect & S4_UNDRAW_2)
		{
			/* Clear choices */
			num_choices = 0;

			/* Loop through cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip cards not in hand */
				if (c->where != LOC_HAND) continue;

				/* Skip cards randomly drawn */
				if (c->random_fake) continue;

				/* Put card design in choice list */
				list[num_choices++] = c->d_ptr;
			}

			/* Try to undraw 2 cards */
			min = 2;

			/* Check for not enough */
			if (num_choices < 2) min = num_choices;

			/* Check for no cards to undraw */
			if (!num_choices) return;

			/* Create prompt */
			printf(prompt,
			        _("Choose cards to return to draw deck"));

			/* Choose 2 to "undraw" */
			p->control->choose(g, g->turn, g->turn, list,
			                   num_choices, min, 2,
			                   undraw_callback, c_ptr->d_ptr,
			                   prompt);
		}

		/* Done */
		return;
	}

	/* Check for loading a ship */
	if (effect & S4_LOAD)
	{
		/* Check for searching draw deck */
		if (effect & S4_SEARCH)
		{
			/* Loop over cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip inactive cards */
				if (!c->active) continue;

				/* Skip non-ships */
				if (!c->d_ptr->capacity) continue;

				/* Skip full ships */
				if (ship_full(g, g->turn, c->d_ptr)) continue;

				/* Count ships */
				ships++;
			}

			/* Loop over cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip cards not in draw deck */
				if (c->where != LOC_DRAW) continue;

				/* Add influence cards */
				if (c->type == TYPE_INFLUENCE)
				{
					/* Add to list */
					list[num_choices++] = c->d_ptr;
				}

				/* Add other cards if ships available */
				else if (ships &&
				         (c->type == TYPE_CHARACTER ||
				          c->type == TYPE_BOOSTER ||
				          c->type == TYPE_SUPPORT))
				{
					/* Add to list */
					list[num_choices++] = c->d_ptr;
				}
			}

			/* Check for no choices */
			if (!num_choices) return;

			/* Prompt */
			printf(prompt, _("Choose card to play or load"));

			/* Choose cards and load them */
			p->control->choose(g, g->turn, g->turn, list,
			                   num_choices, 0, 1, favor_callback,
					   c_ptr->d_ptr, prompt);

			/* Done */
			return;
		}

		/* Determine pile to choose from */
		src = LOC_HAND;

		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip cards in incorrect location */
			if (c->where != src) continue;

			/* Skip cards that aren't loadable */
			if (c->type != TYPE_CHARACTER &&
			    c->type != TYPE_BOOSTER &&
			    c->type != TYPE_SUPPORT) continue;

			/* Add card to list */
			list[num_choices++] = c->d_ptr;
		}

		/* Check for no choices */
		if (!num_choices) return;

		/* Create prompt */
		sprintf(prompt, _("Choose cards to load onto %s"),
		                _(c_ptr->d_ptr->name));

		/* Choose cards and load them */
		p->control->choose(g, g->turn, g->turn, list, num_choices,
		                   0, value, load_callback,
				   c_ptr->d_ptr, prompt);

		/* Done */
		return;
	}

	/* Check for search through a stack */
	if (effect & S4_SEARCH)
	{
		/* XXX May allow different card play */
		p->last_played = 0;

		/* Determine pile to search */
		src = LOC_DRAW;

		/* Check for searching discard pile */
		if (effect & S4_MY_DISCARD) src = LOC_DISCARD;

		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip cards in incorrect location */
			if (c->where != src) continue;

			/* Add card to list */
			list[num_choices++] = c->d_ptr;

			/* Lack of reveal flag will make locations unclear */
			if (!(effect & S4_REVEAL)) c->loc_known = 0;
		}

		/* Check for no choices */
		if (!num_choices) return;

		/* Restrict amount to draw due to hand limit */
		max = hand_limit(g, g->turn) - p->stack[LOC_HAND];

		/* Check for hand limit restriction */
		if (value > max) value = max;

		/* Check for no cards available */
		if (value <= 0) return;
		
		/* Create prompt */
		printf(prompt, _("Choose cards to draw"));

		/* Choose cards and put them in hand */
		p->control->choose(g, g->turn, g->turn, list, num_choices,
		                   value, value, draw_callback,
				   c_ptr->d_ptr, prompt);

		/* Done */
		return;
	}

	/* Check for retrieving card */
	if (effect & S4_RETRIEVE)
	{
		/* XXX May allow different card play */
		p->last_played = 0;

		/* Create type */
		if (effect & S4_MY_CHAR) type |= TYPE_CHARACTER;
		if (effect & S4_MY_BOOSTER) type |= TYPE_BOOSTER;
		if (effect & S4_MY_SUPPORT) type |= TYPE_SUPPORT;

		/* Loop over our cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip cards of wrong type */
			if (!(c->type & type)) continue;

			/* Skip cards not played */
			if (c->where != LOC_COMBAT &&
			    c->where != LOC_SUPPORT) continue;

			/* Check for active qualification */
			if ((effect & S4_ACTIVE) && !c->active) continue;

			/* Never allow retrieving of card just played */
			if (c->d_ptr == c_ptr->d_ptr) continue;

			/* Add design to list */
			list[num_choices++] = c->d_ptr;
		}

		/* Restrict amount to draw due to hand limit */
		max = hand_limit(g, g->turn) - p->stack[LOC_HAND];

		/* Check for hand limit restriction */
		if (value > max) value = max;

		/* Check for no cards available */
		if (value <= 0 || !num_choices) return;
		
		/* Create prompt */
		printf(prompt, ngettext("Choose card to retrieve",
		                         "Choose cards to retrieve", value));

		/* Discard */
		p->control->choose(g, g->turn, g->turn, list, num_choices,
		                   0, value, retrieve_callback, NULL, prompt);

		/* Done */
		return;
	}

	/* Check for shuffle effect */
	if (effect & S4_SHUFFLE)
	{
		/* Check for putting cards from hand into draw pile */
		if (effect & S4_MY_HAND)
		{
			/* Loop over cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip cards not in hand */
				if (c->where != LOC_HAND) continue;

				/* Add card design to list */
				list[num_choices++] = c->d_ptr;
			}

			/* Check for no choices */
			if (!num_choices) return;

			/* Create prompt */
			printf(prompt,
			        _("Choose cards to return to draw pile"));

			/* Ask user to choose cards to place in draw pile */
			p->control->choose(g, g->turn, g->turn, list,
			                   num_choices, 0, value,
			                   undraw_callback, c_ptr->d_ptr,
			                   prompt);
		}

		/* Check for shuffling discard pile into draw pile */
		if (effect & S4_MY_DISCARD)
		{
			/* Loop over cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip cards not in discard */
				if (c->where != LOC_DISCARD) continue;

				/* Reduce discard stack size */
				p->stack[LOC_DISCARD]--;

				/* Put card in draw pile */
				c->where = LOC_DRAW;

				/* Increase draw stack size */
				p->stack[LOC_DRAW]++;

				/* Clear "location known" flag */
				c->loc_known = 0;
			}

			/* Clear last discard pointer */
			p->last_discard = NULL;
		}

		/* Handle category 1 effects */
		notice_effect_1(g);

		/* Done */
		return;
	}

	/* Check for "my cards attack again" */
	if (effect & S4_ATTACK_AGAIN)
	{
		/* No need to do anything but mark character as played */
		p->char_played = 1;

		/* Done */
		return;
	}
}

/*
 * Handle a sacrifice choice.
 */
static int sacrifice_callback(game *g, int who, design **list, int num,
                              void *data)
{
	design *d_ptr = (design *)data;
	player *p;
	card *c;
	int i;
	int effect, value;
	int amt = 0;
	char msg[1024];

	/* Get player pointer */
	p = &g->p[who];

	/* Get special effect code and value */
	effect = d_ptr->special_effect;
	value = d_ptr->special_value;

	/* Check for no cards sacrificed */
	if (!num) return 1;

	/* Loop over cards */
	for (i = 0; i < num; i++)
	{
		/* Find this card */
		c = find_card(g, who, list[i]);

		/* Check for fire value */
		if (effect & S6_FIRE_VALUE)
		{
			/* Add printed value from card */
			amt += c->printed[0];
		}

		/* Check for earth value */
		if (effect & S6_EARTH_VALUE)
		{
			/* Add printed value from card */
			amt += c->printed[1];
		}
	}

	/* Check for requirement satisfied */
	if (effect & (S6_FIRE_VALUE | S6_EARTH_VALUE))
	{
		/* Check for not enough */
		if (amt < value) return 0;

		/* Check for too many cards */
		for (i = 0; i < num; i++)
		{
			/* Find this card */
			c = find_card(g, who, list[i]);

			/* Check for fire value */
			if (effect & S6_FIRE_VALUE)
			{
				/* Check for success without this card */
				if (amt - c->printed[0] >= value) return 0;
			}

			/* Check for earth value */
			if (effect & S6_EARTH_VALUE)
			{
				/* Check for success without this card */
				if (amt - c->printed[1] >= value) return 0;
			}
		}
	}

	/* Check for enough cards */
	else if (num < value) return 0;

	/* Loop over cards and discard */
	for (i = 0; i < num; i++)
	{
		/* Discard */
		move_card(g, who, list[i], LOC_DISCARD, 1);
	}

	/* Check for simulation */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s attracts dragon using %s.\n"),
		             _(p->p_ptr->name), _(d_ptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Attract a dragon */
	attract_dragon(g, who);

	/* Success */
	return 2;
}

/*
 * Handle a "category 6" effect.
 */
static void handle_effect_6(game *g, design *d_ptr)
{
	player *p;
	design *list[DECK_SIZE];
	card *c;
	char prompt[1024];
	int i, num_choices = 0;
	int effect, value;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get special effect code and value */
	effect = d_ptr->special_effect;
	value = d_ptr->special_value;

	/* Check for discard requirement */
	if (effect & S6_DISCARD)
	{
		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip cards not in hand */
			if (c->where != LOC_HAND) continue;

			/* Skip cards with ignored values */
			if (c->value_ignored) continue;

			/* Check for fire value requirement */
			if ((effect & S6_FIRE_VALUE) && !c->printed[0])
			{
				/* Skip card */
				continue;
			}

			/* Check for earth value requirement */
			if ((effect & S6_EARTH_VALUE) && !c->printed[1])
			{
				/* Skip card */
				continue;
			}

			/* Check for character requirement */
			if ((effect & S6_CHAR) &&
			    (c->type != TYPE_CHARACTER))
			{
				/* Skip card */
				continue;
			}

			/* Add design to list */
			list[num_choices++] = c->d_ptr;
		}

		/* Check for no cards to choose */
		if (!num_choices) return;

		/* Create prompt */
		printf(prompt,
		        _("Choose cards to sacrifice to attract dragon"));

		/* Discard */
		p->control->choose(g, g->turn, g->turn, list, num_choices,
		                   0, value, sacrifice_callback, d_ptr, prompt);
	}
}

/*
 * Handle a "category 8" effect.
 */
static void handle_effect_8(game *g, card *c_ptr, int time)
{
	player *p, *opp;
	design *list[DECK_SIZE], *d_ptr;
	card *c;
	char prompt[1024];
	int i, min, num_choices = 0;
	int effect, value;

	/* Check for correct timing */
	if (time != c_ptr->d_ptr->special_time) return;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Get special effect code and value */
	effect = c_ptr->d_ptr->special_effect;
	value = c_ptr->d_ptr->special_value;

	/* Check for forced discard */
	if (effect & S8_YOU_DISCARD)
	{
		/* Loop over deck */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip cards not in hand */
			if (c->where != LOC_HAND) continue;

			/* Put card in list */
			list[num_choices++] = c->d_ptr;
		}

		/* Check for "discard to" flag */
		if (effect & S8_TO)
		{
			/* Cards to discard */
			min = num_choices - value;
		}
		else
		{
			/* Cards to discard */
			min = value;
		}

		/* Check for too few */
		if (min > num_choices) min = num_choices;

		/* Check for no choices */
		if (min <= 0) return;

		/* Discard random cards in simulated games */
		if (g->simulation)
		{
			/* Loop over number to discard */
			for (i = 0; i < min; i++)
			{
				/* Pick a random card from hand */
				d_ptr = random_card(g, !g->turn, LOC_HAND);

				/* Discard it */
				move_card(g, !g->turn, d_ptr, LOC_DISCARD, 0);
			}

			/* Done */
			return;
		}

		/* Create prompt */
		printf(prompt, _("Choose cards to discard"));

		/* Force opponent to discard */
		opp->control->choose(g, !g->turn, !g->turn, list, num_choices,
		                     min, min, discard_callback, c_ptr->d_ptr,
		                     prompt);
	}

	/* Check for disclose */
	if (effect & S8_YOU_DISCLOSE)
	{
		/* Handle simulated game differently */
		if (g->simulation)
		{
			/* Loop over deck */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &opp->deck[i];

				/* Skip cards not in hand */
				if (c->where != LOC_HAND) continue;

				/* Disclose card */
				c->disclosed = 1;
			}

			/* Done */
			return;
		}

		/* Loop over deck */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip cards not in hand */
			if (c->where != LOC_HAND) continue;

			/* Disclose card */
			c->disclosed = 1;

			/* Location is known */
			c->loc_known = 1;
		}

		/* Have AI reevaluate options */
		g->random_event = 1;

		/* Recheck all plays */
		p->last_played = 0;
	}
}

/*
 * Check whether a played card allows us to play more booster/support cards
 * than we could before.
 */
static int check_support_limit(game *g, design *d_ptr)
{
	player *p = &g->p[g->turn];
	card *c;
	int prev = 0, i;

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip card we are checking */
		if (c->d_ptr == d_ptr) continue;

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with text ignored */
		if (c->text_ignored) continue;

		/* Skip cards without category 3 special text */
		if (c->d_ptr->special_cat != 3) continue;

		/* Skip cards without "I may play" */
		if (!(c->d_ptr->special_effect & S3_I_MAY_PLAY)) continue;

		/* Skip cards that do not allow same type as played card */
		if ((c->d_ptr->special_effect & (S3_BOOSTER | S3_SUPPORT)) !=
		    (d_ptr->special_effect & (S3_BOOSTER | S3_SUPPORT)))
			continue;

		/* Check for bigger amount */
		if (prev < c->d_ptr->special_value)
		{
			/* Track biggest */
			prev = c->d_ptr->special_value;
		}
	}

	/* Return true if card helpful */
	return d_ptr->special_value > prev;
}

/*
 * Play a card into the appropriate area.
 */
void play_card(game *g, design *d_ptr, int no_effect, int check)
{
	char msg[1024];
	player *p, *opp;
	card *c, *old;
	int i, gang_good, from_ship = 0;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, no_effect ? _("%s plays %s with no effect.\n") :
		                         _("%s plays %s.\n"),
		             _(g->p[g->turn].p_ptr->name), _(d_ptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip incorrect cards */
		if (c->d_ptr != d_ptr) continue;

		/* Reduce size of source stack */
		p->stack[c->where]--;

		/* Put card in correct spot */
		if (d_ptr->type == TYPE_CHARACTER ||
		    d_ptr->type == TYPE_BOOSTER)
		{
			/* Put card in combat area */
			c->where = LOC_COMBAT;
		}
		else if (d_ptr->type == TYPE_SUPPORT)
		{
			/* Put card in support area */
			c->where = LOC_SUPPORT;
		}
		else if (d_ptr->type == TYPE_LEADERSHIP)
		{
			/* Put card in leadership area */
			c->where = LOC_LEADERSHIP;

			/* Remember last leadership card */
			p->last_leader = c->d_ptr;
		}
		else
		{
			/* Put card in influence area */
			c->where = LOC_INFLUENCE;
		}

		/* Increase destination stack size */
		p->stack[c->where]++;

		/* Played cards lose disclosed flag */
		c->disclosed = 0;

		/* Card's location is known */
		c->loc_known = 1;

		/* Done looking */
		break;
	}

	/* Check for first to run out of cards */
	if (p->stack[LOC_HAND] + p->stack[LOC_DRAW] == 0)
	{
		/* Check for first */
		if (!g->p[!g->turn].no_cards) p->no_cards = 1;
	}

	/* Played cards are active */
	c->active = 1;

	/* Card was recently played */
	c->recent = 1;

	/* Characters deactivate cards underneath them */
	if (!p->char_played && d_ptr->type == TYPE_CHARACTER)
	{
		/* Assume all previous cards are in same gang */
		gang_good = 1;

		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			old = &p->deck[i];

			/* Skip non-combat cards */
			if (old->where != LOC_COMBAT) continue;

			/* Skip inactive cards */
			if (!old->active) continue;

			/* Check for no gang icons at all */
			if (!(c->icons & ICON_GANG_MASK))
			{
				/* Gang is not allowed */
				gang_good = 0;
				break;
			}

			/* Check for matching gang */
			if ((c->icons & ICON_GANG_MASK) !=
			    (old->icons & ICON_GANG_MASK))
			{
				/* Gang icons don't match */
				gang_good = 0;
			}
		}

		/* Loop over cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			old = &p->deck[i];

			/* Skip non-combat cards */
			if (old->where != LOC_COMBAT) continue;

			/* Don't deactivate just-played card */
			if (old->d_ptr == d_ptr) continue;

			/* Don't deactivate anything if gangs match */
			if (gang_good) continue;

			/* Deactivate card */
			deactivate_card(old);
		}

		/* Player has played needed character for this turn */
		p->char_played = 1;
	}

	/* Loop over other active cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		old = &p->deck[i];

		/* Skip card we are playing */
		if (old->d_ptr == d_ptr) continue;

		/* Skip inactive cards */
		if (!old->active) continue;

		/* Check category 1 effects */
		if (old->d_ptr->special_cat == 1)
		{
			/* Check for "one character" effect */
			if ((old->d_ptr->special_effect & S1_ONE_CHAR) &&
			    d_ptr->type == TYPE_CHARACTER) old->target = NULL;

			/* Check for "one booster" effect */
			if ((old->d_ptr->special_effect & S1_ONE_BOOSTER) &&
			    d_ptr->type == TYPE_BOOSTER) old->target = NULL;

			/* Check for "one support" effect */
			if ((old->d_ptr->special_effect & S1_ONE_SUPPORT) &&
			    d_ptr->type == TYPE_SUPPORT) old->target = NULL;
		}

		/* Check category 3 effects */
		if (old->d_ptr->special_cat == 3)
		{
			/* Check for unused "play as free" effect */
			if (!old->used &&
			    old->d_ptr->special_effect & S3_AS_FREE)
			{
				/* Play as free card is used */
				old->used = 1;

				/* Attempt to play card as FREE */
				c->playing_free = 1;

				/* Notice special texts */
				notice_effect_1(g);

				/* Check for successful */
				if (c->playing_free)
				{
					/* Mark card as played free */
					c->was_played_free = 1;

					/* No longer playing as free */
					c->playing_free = 0;
				}
			}
		}
	}

	/* Check for played from landed ship */
	if (c->ship)
	{
		/* No longer played from ship */
		c->ship = NULL;

		/* Card is played from a ship */
		from_ship = 1;

		/* Check for Buka character */
		if (c->d_ptr->people == 8 && c->type == TYPE_CHARACTER)
		{
			/* Try to play as FREE */
			c->playing_free = 1;

			/* Notice special texts */
			notice_effect_1(g);

			/* Check for successful */
			if (c->playing_free)
			{
				/* Mark card as played free */
				c->was_played_free = 1;

				/* No longer playing as free */
				c->playing_free = 0;
			}
		}
	}

	/* Notice special power texts */
	notice_effect_1(g);

	/* Check if newly placed card has text ignored */
	if (!c->text_ignored)
	{
		/* Check for "I may play" effects */
		if (d_ptr->special_cat == 3 &&
		    (d_ptr->special_effect & S3_I_MAY_PLAY))
		{
			/* Check for playing next card as free */
			if (d_ptr->special_effect & S3_AS_FREE)
			{
				/* Recheck all plays */
				p->last_played = 0;
			}

			/* Check for allowance for extra booster/support */
			if (p->phase == PHASE_SUPPORT &&
			    (d_ptr->special_effect & (S3_BOOSTER | S3_SUPPORT)))
			{
				/* Check for bigger limit than before */
				if (check_support_limit(g, d_ptr))
				{
					/* Recheck all plays */
					p->last_played = 0;
				}
			}
		}

		/* Preventing bluff from being called allows extra plays */
		if (d_ptr->special_cat == 3 && 
		    (d_ptr->special_effect == (S3_YOU_MAY_NOT | S3_CALL_BLUFF)))
		{
			/* Recheck all plays */
			p->last_played = 0;
		}

		/* Check for category 4 effect */
		if (d_ptr->special_cat == 4 && !no_effect)
		{
			/* Check for "if from ship" restriction */
			if (d_ptr->special_effect & S4_IF_FROM_SHIP)
			{
				/* Only handle effect if from ship */
				if (from_ship) handle_effect_4(g, c, TIME_NOW);
			}
			else
			{
				/* Handle effect */
				handle_effect_4(g, c, TIME_NOW);
			}
		}

		/* Check for category 5 effect */
		if (d_ptr->special_cat == 5)
		{
			/* Check for element swap */
			if (d_ptr->special_effect & S5_ELEMENT_SWAP)
			{
				/* Swap element */
				g->fight_element = !g->fight_element;
			}

			/* Check for "play as free" */
			if ((d_ptr->special_effect & S5_PLAY_FREE_IF) &&
			    card_text_matches(g, d_ptr))
			{
				/* Attempt to play as free */
				c->playing_free = 1;

				/* Notice effects */
				notice_effect_1(g);

				/* Check for still free */
				if (c->playing_free)
				{
					/* Play card as free */
					c->was_played_free = 1;

					/* No longer playing as free */
					c->playing_free = 0;
				}
			}

			/* Notice special text */
			notice_effect_1(g);
		}

		/* Check for dragon attraction */
		if (d_ptr->special_cat == 6)
		{
			/* Handle effect */
			handle_effect_6(g, d_ptr);
		}

		/* Check for force opponent */
		if (d_ptr->special_cat == 8 && !no_effect)
		{
			/* Handle effect */
			handle_effect_8(g, c, TIME_NOW);
		}
	}

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Check for "forced play" effects from opponent */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards without category 7 text */
		if (c->d_ptr->special_cat != 7) continue;

		/* Check for "play support" */
		if (c->d_ptr->special_effect & S7_PLAY_SUPPORT &&
		    d_ptr->type == TYPE_SUPPORT) c->used = 1;

		/* Check for "play booster" */
		if (c->d_ptr->special_effect & S7_PLAY_BOOSTER &&
		    d_ptr->type == TYPE_BOOSTER) c->used = 1;
	}

	/* Check special text targets and possibly ask to disambiguate */
	check_targets(g, g->turn, check);
}

/*
 * Check if a bluff card can be played.
 *
 * Bluff cards that would be immediately ignored are illegal.
 */
int bluff_legal(game *g, int who)
{
	player *opp;
	card *c;
	int i;

	/* Get opponent pointer */
	opp = &g->p[!who];

	/* Loop over opponent cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with text ignored */
		if (c->text_ignored) continue;

		/* Skip cards that do not have category 1 text */
		if (c->d_ptr->special_cat != 1) continue;

		/* Skip cards that do not ignore support card values */
		if (!(c->d_ptr->special_effect & S1_IGNORE)) continue;
		if (!(c->d_ptr->special_effect & S1_ALL_SUPPORT)) continue;
		if (!(c->d_ptr->special_effect & S1_FIRE_VAL)) continue;

		/* Bluff cards are ignored */
		return 0;
	}

	/* Bluff cards are not ignored */
	return 1;
}

/*
 * Play a card as a bluff.
 */
void play_bluff(game *g, design *d_ptr)
{
	char msg[1024];
	player *p, *opp;
	card *c;
	int i;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s plays bluff card.\n"), p->p_ptr->name);

		/* Send message */
		message_add(msg);
	}

	/* Find card to be played */
	c = find_card(g, g->turn, d_ptr);

	/* Move card to support area */
	move_card(g, g->turn, d_ptr, LOC_SUPPORT, 0);

	/* Set bluff flag */
	c->bluff = 1;

	/* Override type to support */
	c->type = TYPE_SUPPORT;

	/* Card is active */
	c->active = 1;

	/* Card is recently played */
	c->recent = 1;

	/* Card's location is unknown */
	c->loc_known = 0;

	/* Check for card played from ship */
	if (c->ship)
	{
		/* Card is no longer on ship */
		c->ship = NULL;

		/* Card's location is known */
		c->loc_known = 1;
	}
	else
	{
		/* Loop over cards in hand */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip cards not in hand */
			if (c->where != LOC_HAND) continue;

			/* Don't clear known flag of disclosed cards */
			if (c->disclosed) continue;

			/* Clear location known flag */
			c->loc_known = 0;
		}
	}

	/* Check for "forced play" effects from opponent */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards without category 7 text */
		if (c->d_ptr->special_cat != 7) continue;

		/* Check for "play support" */
		if (c->d_ptr->special_effect & S7_PLAY_SUPPORT) c->used = 1;
	}

	/* Notice special texts */
	notice_effect_1(g);
}

/*
 * Reveal a bluff card and discard it.
 *
 * We return true if the card's bluff icon matches the current fight element.
 */
int reveal_bluff(game *g, int who, design *d_ptr)
{
	player *p;
	card *c;
	char msg[1024];
	int good = 0;

	/* Get player pointer */
	p = &g->p[who];

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s reveals bluff card %s.\n"), p->p_ptr->name,
		                                               d_ptr->name);

		/* Send message */
		message_add(msg);
	}

	/* Get card */
	c = find_card(g, who, d_ptr);

	/* Check for good bluff */
	if (!g->fight_element && (c->icons & ICON_BLUFF_F)) good = 1;
	if (g->fight_element && (c->icons & ICON_BLUFF_E)) good = 1;

	/* Card is no longer a bluff */
	c->bluff = 0;

	/* Reset card type */
	c->type = c->d_ptr->type;

	/* Move card to discard pile */
	move_card(g, who, d_ptr, LOC_DISCARD, 1);

	/* Notice special texts */
	notice_effect_1(g);

	/* Return good flag */
	return good;
}

/*
 * A bluff is called.
 */
void bluff_called(game *g)
{
	player *p, *opp;
	card *c;
	char msg[1024];
	int i, good = 1;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s calls bluff.\n"), opp->p_ptr->name);

		/* Send message */
		message_add(msg);
	}

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip non-bluff cards */
		if (!c->bluff) continue;

		/* Reveal bluff card */
		if (!reveal_bluff(g, g->turn, c->d_ptr)) good = 0;
	}

	/* Bluff was good */
	if (good)
	{
		/* Message */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg,
			       _("%s attracts dragon with successful bluff.\n"),
			       p->p_ptr->name);

			/* Send message */
			message_add(msg);
		}

		/* Attract a dragon */
		attract_dragon(g, g->turn);
	}
	else
	{
		/* Message */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg,
			       _("%s attracts dragon for calling bluff.\n"),
			       opp->p_ptr->name);

			/* Send message */
			message_add(msg);
		}

		/* Opponent attracts dragon */
		attract_dragon(g, !g->turn);

		/* Current player must retreat */
		if (!g->game_over) retreat(g);
	}
}

/*
 * Load a card onto the given ship.
 */
void load_card(game *g, design *d_ptr, design *ship_dptr)
{
	player *p;
	card *c;
	char msg[1024];

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s loads %s onto %s.\n"), p->p_ptr->name,
		        _(d_ptr->name), _(ship_dptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Find card to be loaded */
	c = find_card(g, g->turn, d_ptr);

	/* Reduce source stack size */
	p->stack[c->where]--;

	/* Put card in influence area */
	c->where = LOC_INFLUENCE;

	/* Increase size of influence area */
	p->stack[LOC_INFLUENCE]++;

	/* Set ship */
	c->ship = ship_dptr;

	/* Loaded cards are not disclosed */
	c->disclosed = 0;

	/* Card's location is known */
	c->loc_known = 1;

	/* Card was recently played */
	c->recent = 1;

	/* Check for running out of cards */
	if (p->stack[LOC_HAND] + p->stack[LOC_DRAW] == 0)
	{
		/* Check for first */
		if (!g->p[!g->turn].no_cards) p->no_cards = 1;
	}

	/* Notice special power texts */
	notice_effect_1(g);
}

/*
 * Land a ship.
 */
void land_ship(game *g, design *d_ptr)
{
	player *p;
	card *c;
	char msg[1024];

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s lands %s.\n"), p->p_ptr->name,
		        _(d_ptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Find card */
	c = find_card(g, g->turn, d_ptr);

	/* Land ship */
	c->landed = 1;
}

/*
 * Return TRUE if a card's "on my turn" special text would have any effect.
 *
 * This is used to reduce AI tree searching.
 */
int special_possible(game *g, design *d_ptr)
{
	player *opp;
	card *c;
	int effect, type = 0;
	int num_choices = 0, num_char = 0;
	int i;

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Get effect code */
	effect = d_ptr->special_effect;

	/* Check for not "on my turn" */
	if (d_ptr->special_time != TIME_MYTURN) return 0;

	/* Check for not category 4 effect */
	if (d_ptr->special_cat != 4) return 0;

	/* Check for discard of active cards */
	if (effect & (S4_YOUR_CHAR | S4_YOUR_SUPPORT | S4_YOUR_BOOSTER))
	{
		/* Create type */
		if (effect & S4_YOUR_CHAR) type |= TYPE_CHARACTER;
		if (effect & S4_YOUR_BOOSTER) type |= TYPE_BOOSTER;
		if (effect & S4_YOUR_SUPPORT) type |= TYPE_SUPPORT;

		/* Loop over opponent cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Skip non-characters */
			if (c->type != TYPE_CHARACTER) continue;

			/* Conut characters */
			num_char++;
		}

		/* Check for forbidden to discard last character */
		if ((effect & S4_NOT_LAST_CHAR) && num_char == 1)
		{
			/* Remove character from type mask */
			type &= ~TYPE_CHARACTER;
		}

		/* Loop over opponent cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip non-active cards */
			if (!c->active) continue;

			/* Skip cards of wrong type */
			if (!(c->type & type)) continue;

			/* Skip active cards with PROTECTED */
			if (c->active && (c->icons & ICON_PROTECTED)) continue;

			/* Check for "with icon" restriction */
			if (effect & S4_WITH_ICON)
			{
				/* Check for no icons depicted */
				if (!c->d_ptr->icons) continue;
			}

			/* Count choices */
			num_choices++;
		}

		/* Check for no discards allowed */
		if (!num_choices) return 0;

		/* Card can be used */
		return 1;
	}

	/* Unknown effect */
	return 0;
}

/*
 * Use a card's special effect.
 *
 * This only applies to "On my turn..." cards.
 */
void use_special(game *g, design *d_ptr)
{
	card *c;
	char msg[1024];

	/* Check for simulation */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s uses %s special text.\n"),
		             _(g->p[g->turn].p_ptr->name), _(d_ptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Find card used */
	c = find_card(g, g->turn, d_ptr);

	/* Card is used */
	c->used = 1;

	/* Check for category 4 effect */
	if (d_ptr->special_cat == 4)
	{
		/* Handle effect */
		handle_effect_4(g, c, TIME_MYTURN);
	}
}

/*
 * Check that a set of discards satisifes a category 7 card.
 */
static int satisfy_legal(game *g, int who, design **list, int num, int which,
                         int effect, int v, int top)
{
	player *p;
	card *c;
	int amt_fire = 0, amt_earth = 0, n = 0;
	int i, x;

	/* Restrict 'which' bitmask to legal bits */
	which &= (1 << num) - 1;

	/* Get player pointer */
	p = &g->p[who];

	/* Check for "discard character" */
	if (effect & S7_DISCARD_CHAR)
	{
		/* Loop over cards */
		for (i = 0; i < num; i++)
		{
			/* Skip cards not selected */
			if (!(which & (1 << i))) continue;

			/* Count cards selected */
			n++;

			/* Check for non-character */
			if (list[i]->type != TYPE_CHARACTER) return 0;
		}

		/* Check for wrong number of cards selected */
		if (v != n) return 0;

		/* Assume legal */
		return 1;
	}

	/* Check for subsets from top level */
	if (top && effect != S7_DISCARD_BOTH)
	{
		/* Try set with each card removed */
		for (i = 0; i < num; i++)
		{
			/* Check for card not in set */
			if (!(which & (1 << i))) continue;

			/* Check this subset */
			if (satisfy_legal(g, who, list, num, which & ~(1 << i),
			                  effect, v, 0))
			{
				/* Illegal to give too many cards */
				return 0;
			}
		}
	}

	/* Loop over cards */
	for (i = 0; i < num; i++)
	{
		/* Skip cards not selected */
		if (!(which & (1 << i))) continue;

		/* Find card */
		c = find_card(g, who, list[i]);

		/* Count total values */
		amt_fire += c->printed[0];
		amt_earth += c->printed[1];
	}

	/* Check for simple requirements */
	if (effect & (S7_DISCARD_FIRE | S7_DISCARD_EARTH | S7_DISCARD_EITHER))
	{
		/* Check for discards in one element */
		if ((effect & S7_DISCARD_FIRE) && amt_fire < v) return 0;
		if ((effect & S7_DISCARD_EARTH) && amt_earth < v) return 0;

		/* Check for "discard either" */
		if (effect & S7_DISCARD_EITHER)
		{
			/* Check for both values too small */
			if (amt_fire < v && amt_earth < v) return 0;
		}

		/* Discard is legal */
		return 1;
	}

	/* Divide cards into two sets for "discard both" */
	for (i = 1; i < 1 << num; i++)
	{
		/* Check for too many selected */
		if ((i | which) != which) continue;

		/* Compute second subset */
		x = ~i & which;

		/* Check this division */
		if (satisfy_legal(g, who, list, num, i, S7_DISCARD_FIRE, v, 1)&&
		    satisfy_legal(g, who, list, num, x, S7_DISCARD_EARTH, v, 1))
		{
			/* Success */
			return 1;
		}
	}

	/* Must not be legal */
	return 0;
}

/*
 * Handle a satisfy choice.
 */
static int satisfy_callback(game *g, int who, design **list, int num,
                            void *data)
{
	design *d_ptr = (design *)data;
	player *p;
	card *c;
	char msg[1024];
	int i;
	int effect, value;

	/* Get player pointer */
	p = &g->p[who];

	/* Find opponent's card */
	c = find_card(g, !who, d_ptr);

	/* Get special effect code and value */
	effect = d_ptr->special_effect;
	value = d_ptr->special_value;

	/* Check for boosted effect */
	if (c->text_boosted) value *= 2;

	/* Check for illegal discards */
	if (!satisfy_legal(g, who, list, num, ~0, effect, value, 1)) return 0;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s satisfies %s.\n"), _(p->p_ptr->name),
		                                      _(d_ptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Discard each chosen card */
	for (i = 0; i < num; i++)
	{
		/* Discard */
		move_card(g, who, list[i], LOC_DISCARD, 1);
	}

	/* Mark card as satisfied */
	c->used = 1;

	/* Success */
	return 2;
}

/*
 * Check whether a player has cards needed to satisfy a "discard or..."
 * effect.
 */
int satisfy_possible(game *g, design *d_ptr)
{
	player *p;
	card *c;
	design *list[DECK_SIZE];
	int i;
	int effect, value;
	int n = 0;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Find card */
	c = find_card(g, !g->turn, d_ptr);

	/* Get effect code and value */
	effect = d_ptr->special_effect;
	value = d_ptr->special_value;

	/* Check for boosted effect */
	if (c->text_boosted) value *= 2;

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not in hand */
		if (c->where != LOC_HAND) continue;

		/* Skip random cards */
		if (c->random_fake) continue;

		/* Skip cards with ignored values */
		if (c->value_ignored &&
		    (effect & (S7_DISCARD_EARTH | S7_DISCARD_FIRE |
		               S7_DISCARD_BOTH | S7_DISCARD_EITHER)))
		{
			/* Skip card */
			continue;
		}

		/* Add card to list */
		list[n++] = c->d_ptr;
	}

	/* Loop over card combinations */
	for (i = 1; i < 1 << n; i++)
	{
		/* Check legality */
		if (satisfy_legal(g, g->turn, list, n, i, effect, value, 1))
		{
			/* Satisfaction is possible */
			return 1;
		}
	}

	/* Not possible */
	return 0;
}

/*
 * Satisfy opponent's "discard or..." special effect.
 *
 * We mark the opponent's card as "used" when done.
 */
void satisfy_discard(game *g, design *d_ptr)
{
	player *p;
	design *list[DECK_SIZE];
	card *c;
	char prompt[1024];
	int i, num_choices = 0;
	int effect;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get effect code */
	effect = d_ptr->special_effect;

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not in hand */
		if (c->where != LOC_HAND) continue;

		/* Check for fire requirement and no fire value */
		if ((effect & S7_DISCARD_FIRE) && !c->printed[0]) continue;

		/* Check for earth requirement and no earth value */
		if ((effect & S7_DISCARD_EARTH) && !c->printed[1]) continue;

		/* Check for fire/earth requirement and no values */
		if ((effect & (S7_DISCARD_EITHER | S7_DISCARD_BOTH)) &&
		    !c->printed[0] && !c->printed[1]) continue;

		/* Check for fire/earth requirement and value ignored */
		if ((effect & (S7_DISCARD_EITHER | S7_DISCARD_BOTH |
		               S7_DISCARD_FIRE | S7_DISCARD_EARTH)) &&
		    c->value_ignored) continue;

		/* Check for character requirement and not character */
		if ((effect & S7_DISCARD_CHAR) &&
		    (c->d_ptr->type != TYPE_CHARACTER)) continue;

		/* Add design to list */
		list[num_choices++] = c->d_ptr;
	}

	/* Create prompt */
	sprintf(prompt, _("Choose cards to satisfy %s"), _(d_ptr->name));

	/* Discard */
	p->control->choose(g, g->turn, g->turn, list, num_choices,
	                   1, num_choices, satisfy_callback, d_ptr, prompt);
}

/*
 * Check for unsatisfied "discard ... or I attract dragon" card.
 *
 * If so, attract a dragon.
 */
static void check_unsatisfied_attract(game *g, int who)
{
	player *opp;
	card *c;
	char msg[1024];
	int i;

	/* Get opponent pointer */
	opp = &g->p[!who];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Check for forced retreat effects */
		if (c->d_ptr->special_cat != 7) continue;

		/* Skip ignored texts */
		if (c->text_ignored) continue;

		/* Skip satisfied cards */
		if (c->used) continue;

		/* Check for "or dragon" */
		if (c->d_ptr->special_effect & S7_OR_DRAGON)
		{
			/* Check for simulation */
			if (!g->simulation)
			{
				/* Format message */
				sprintf(msg,
				        _("%s attracts dragon due to %s.\n"),
				        _(g->p[!who].p_ptr->name),
				        _(c->d_ptr->name));

				/* Send message */
				message_add(msg);
			}

			/* Award dragon to opponent */
			attract_dragon(g, !who);

			/* Check for end of game */
			if (g->game_over) return;
		}
	}
}

/*
 * Clear both player's combat and support areas after a fight.
 *
 * Also deactivate leadership cards.
 */
void clear_cards(game *g)
{
	player *p;
	card *c;
	int i, j;

	/* Loop over players */
	for (i = 0; i < 2; i++)
	{
		/* Player pointer */
		p = &g->p[i];

		/* Loop through cards */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Influence cards are not cleared or deactivated */
			if (c->type == TYPE_INFLUENCE)
			{
				/* Card is no longer recent */
				c->recent = 0;

				/* Do not deactivate */
				continue;
			}

			/* Handle bluff cards */
			if (c->bluff)
			{
				/* Reveal card */
				reveal_bluff(g, i, c->d_ptr);

				/* Next */
				continue;
			}

			/* Deactivate card */
			deactivate_card(c);

			/* Check for card to be removed */
			if (c->where == LOC_COMBAT ||
			    c->where == LOC_SUPPORT)
			{
				/* Reduce old stack size */
				p->stack[c->where]--;

				/* Move to discard */
				c->where = LOC_DISCARD;

				/* Increase discard stack size */
				p->stack[LOC_DISCARD]++;

				/* Track last discard */
				p->last_discard = c->d_ptr;
			}
		}
	}

	/* Notice special power texts */
	notice_effect_1(g);
}

/*
 * Start current player's turn.
 */
void start_turn(game *g)
{
	player *p;
	card *c;
	int i, storm = 0;
	char msg[1024];

	/* Get current player pointer */
	p = &g->p[g->turn];

	/* No character played */
	p->char_played = 0;

	/* No cards drawn */
	p->cards_drawn = 0;

	/* No card last played */
	p->last_played = 0;

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Card not used yet */
		c->used = 0;
	}

	/* Look for active storm cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip non-category 6 cards */
		if (c->d_ptr->special_cat != 6) continue;

		/* Skip cards with text ignored */
		if (c->text_ignored) continue;

		/* Skip cards without STORM flag */
		if (!(c->d_ptr->special_effect & S6_STORM)) continue;

		/* Count storms */
		storm++;
	}

	/* Check for multiple storms */
	if (storm > 1)
	{
		/* Check for simulation */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg, _("%s attracts dragon using Storms.\n"),
			             _(g->p[g->turn].p_ptr->name));

			/* Send message */
			message_add(msg);
		}

		/* Attract a dragon */
		attract_dragon(g, g->turn);
	}
}

/*
 * Handle end of game.
 */
void game_over(game *g)
{
	int winner;
	char buf[1024];

	/* Do nothing if already handled */
	if (g->game_over) return;

	/* End any fight in progress */
	g->fight_started = 0;

	/* Set game over flag */
	g->game_over = 1;

	/* Determine winner */
	if (g->p[0].dragons) winner = 0;
	else if (g->p[1].dragons) winner = 1;
	else
	{
		/* Check for no cards */
		if (g->p[0].no_cards) winner = 1;
		else winner = 0;
	}

	/* Award crystals */
	g->p[winner].crystals += g->p[winner].dragons + 1;

	/* Crystals can't go over 5 */
	if (g->p[winner].crystals > 5) g->p[winner].crystals = 5;

	/* Check for simulation */
	if (!g->simulation)
	{
		/* Send message */
		message_add(_("Game over\n"));

		/* Create message about crystals won */
		sprintf(buf, ngettext("%s wins %d crystal.\n",
		                      "%s wins %d crystals.\n",
				      g->p[winner].dragons + 1),
		        _(g->p[winner].p_ptr->name), g->p[winner].dragons + 1);

		/* Send message */
		message_add(buf);
	}
}

/*
 * Attract a dragon.
 *
 * This can result in the end of the game.
 */
void attract_dragon(game *g, int who)
{
	/* First remove opponent's dragons if any */
	if (g->p[!who].dragons)
	{
		/* Remove a dragon */
		g->p[!who].dragons--;

		/* Done */
		return;
	}

	/* Check for game won */
	if (g->p[who].dragons == 3)
	{
		/* Game is over */
		game_over(g);

		/* Set instant win flag */
		g->p[who].instant_win = 1;

		/* Done */
		return;
	}

	/* Add a dragon to us */
	g->p[who].dragons++;
}

/*
 * Return number of dragons attracted if the current player retreats.
 */
static int dragon_amount(game *g)
{
	player *p, *opp;
	card *c;
	int effect, value;
	int i, prio;
	int n, dragons = 1;

	/* Player pointer */
	p = &g->p[g->turn];

	/* Opponent player */
	opp = &g->p[!g->turn];

	/* Count cards */
	n = opp->stack[LOC_COMBAT] + opp->stack[LOC_SUPPORT];

	/* Loop over opponent cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Bluff cards do not count towards 6 */
		if (c->bluff) n--;
	}

	/* Six or more means 2 dragons */
	if (n >= 6) dragons = 2;

	/* Loop over priorities */
	for (prio = 1; prio <= 4; prio++)
	{
		/* Loop over opponent's cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &opp->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Skip cards with ignored text */
			if (c->text_ignored) continue;

			/* Skip cards without "category 2" effects */
			if (c->d_ptr->special_cat != 2) continue;

			/* Skip cards with wrong priority */
			if (c->d_ptr->special_prio != prio) continue;

			/* Get effect code and value */
			effect = c->d_ptr->special_effect;
			value = c->d_ptr->special_value;

			/* Skip cards that aren't "if you retreat" */
			if (!(effect & S2_YOU_RETREAT)) continue;

			/* Check for "additional dragon" effect */
			if (effect & S2_ADDITIONAL) dragons += value;

			/* Check for "fewer dragons" effect */
			if (effect & S2_FEWER)
			{
				/* Reduce dragons */
				dragons -= value;

				/* Dragons can't go below 0 */
				if (dragons < 0) dragons = 0;
			}

			/* Check for "exactly" effect */
			if (effect & S2_EXACTLY) dragons = value;

			/* Check for "no more than" effect */
			if (effect & S2_NO_MORE_THAN)
			{
				/* Check for too many */
				if (dragons > value) dragons = value;
			}
		}

		/* Loop over our cards */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip inactive cards */
			if (!c->active) continue;

			/* Skip cards with ignored text */
			if (c->text_ignored) continue;

			/* Skip cards without "category 2" effects */
			if (c->d_ptr->special_cat != 2) continue;

			/* Skip cards with wrong priority */
			if (c->d_ptr->special_prio != prio) continue;

			/* Get effect code and value */
			effect = c->d_ptr->special_effect;
			value = c->d_ptr->special_value;

			/* Skip cards that aren't "if I retreat" */
			if (!(effect & S2_I_RETREAT)) continue;

			/* Check for "additional dragon" effect */
			if (effect & S2_ADDITIONAL) dragons += value;

			/* Check for "fewer dragons" effect */
			if (effect & S2_FEWER)
			{
				/* Reduce dragons */
				dragons -= value;

				/* Dragons can't go below 0 */
				if (dragons < 0) dragons = 0;
			}

			/* Check for "exactly" effect */
			if (effect & S2_EXACTLY) dragons = value;

			/* Check for "no more than" effect */
			if (effect & S2_NO_MORE_THAN)
			{
				/* Check for too many */
				if (dragons > value) dragons = value;
			}
		}
	}

	/* Return number of dragons */
	return dragons;
}

/*
 * Handle a choice of cards to discard when declining a fight.
 */
static int decline_callback(game *g, int who, design **list, int num,
                            void *data)
{
	player *p;
	design *d_ptr;
	int i, dest;

	/* Loop over cards */
	for (i = 0; i < num; i++)
	{
		/* Check for leadership */
		if (list[i]->type == TYPE_LEADERSHIP)
		{
			/* Discarded leadership go to leadership pile */
			dest = LOC_LEADERSHIP;
		}
		else
		{
			/* Everything else goes to discard pile */
			dest = LOC_DISCARD;
		}

		/* Discard */
		move_card(g, who, list[i], dest, 1);
	}

	/* Get player pointer */
	p = &g->p[who];

	/* Refresh our hand */
	while (p->stack[LOC_HAND] < 6)
	{
		/* Get a card from draw pile */
		d_ptr = random_card(g, who, LOC_DRAW);

		/* Stop if cards run out */
		if (!d_ptr) break;

		/* Put card in hand */
		move_card(g, g->turn, d_ptr, LOC_HAND, 0);
	}

	/* Check for end of game */
	if (!p->stack[LOC_HAND]) game_over(g);

	/* No longer our action */
	p->phase = PHASE_NONE;

	/* Play goes to other player */
	g->turn = !g->turn;

	/* Player pointer */
	p = &g->p[g->turn];

	/* Start next turn */
	p->phase = PHASE_START;

	/* Success */
	return 1;
}


/*
 * Retreat.
 *
 * If no fight is in progress, we need to discard 1-3 cards.
 */
void retreat(game *g)
{
	char msg[1024];
	player *p;
	design *d_ptr, *list[DECK_SIZE];
	card *c;
	int num_choices = 0;
	int i;
	int dragons = 1;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Loop through our cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip unlanded ships */
		if (!c->landed) continue;

		/* Discard landed ship */
		move_card(g, g->turn, c->d_ptr, LOC_DISCARD, 1);

		/* Ship is no longer landed */
		c->landed = 0;
	}

	/* Check for fight started */
	if (g->fight_started)
	{
		/* Check for unsatisfied dragon attraction cards */
		check_unsatisfied_attract(g, g->turn);

		/* Check for game over */
		if (g->game_over) return;

		/* Get number of dragons attracted */
		dragons = dragon_amount(g);

		/* Message */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg, _("%s retreats.\n"),
			        _(g->p[g->turn].p_ptr->name));

			/* Send message */
			message_add(msg);

			/* Format message */
			sprintf(msg, ngettext("%s attracts %d dragon.\n",
			                      "%s attracts %d dragons.\n",
			                      dragons),
			        _(g->p[!g->turn].p_ptr->name), dragons);

			/* Send message */
			message_add(msg);
		}

		/* Attract dragons */
		for (i = 0; i < dragons; i++)
		{
			/* Attract a dragon */
			attract_dragon(g, !g->turn);

			/* Check for game over */
			if (g->game_over) return;
		}

		/* Clear support and combat areas */
		clear_cards(g);

		/* Get opponent's player pointer */
		p = &g->p[!g->turn];

		/* Refresh opponent's hand */
		while (p->stack[LOC_HAND] < 6)
		{
			/* Get a card from draw pile */
			d_ptr = random_card(g, !g->turn, LOC_DRAW);

			/* Stop if cards run out */
			if (!d_ptr) break;

			/* Put card in hand */
			move_card(g, !g->turn, d_ptr, LOC_HAND, 0);
		}

		/* Check for end of game */
		if (!p->stack[LOC_HAND]) game_over(g);

		/* Get our pointer */
		p = &g->p[g->turn];

		/* Refresh our hand */
		while (p->stack[LOC_HAND] < 6)
		{
			/* Get a card from draw pile */
			d_ptr = random_card(g, g->turn, LOC_DRAW);

			/* Stop if cards run out */
			if (!d_ptr) break;

			/* Put card in hand */
			move_card(g, g->turn, d_ptr, LOC_HAND, 0);
		}

		/* Check for end of game */
		if (!p->stack[LOC_HAND]) game_over(g);

		/* Handle category 1 effects */
		notice_effect_1(g);

		/* Clear fight started flag */
		g->fight_started = 0;

		/* Retreating player plays again, but at beginning */
		p->phase = PHASE_START;

		/* Done */
		return;
	}

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, _("%s declines to start fight.\n"),
		        _(g->p[g->turn].p_ptr->name));

		/* Send message */
		message_add(msg);
	}

	/* Clear played leadership cards */
	clear_cards(g);

	/* Get declining player */
	p = &g->p[g->turn];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not in hand */
		if (c->where != LOC_HAND) continue;

		/* Add card design to list */
		list[num_choices++] = c->d_ptr;
	}

	/* Check for cards to discard */
	if (num_choices > 0)
	{
		/* Ask player to discard */
		p->control->choose(g, g->turn, g->turn, list, num_choices, 1, 3,
				   decline_callback, NULL,
		                   _("Choose cards to discard"));
	}
	else
	{
		/* Game over */
		game_over(g);
	}

	/* Handle category 1 effects */
	notice_effect_1(g);
}

/*
 * Compute the power a player has in the current fight element.
 */
int compute_power(game *g, int who)
{
	player *p;
	card *c;
	int power = 0;
	int i;

	/* Get player pointer */
	p = &g->p[who];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip non-combat and non-support cards */
		if (c->where != LOC_COMBAT && c->where != LOC_SUPPORT) continue;

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with ignored values */
		if (c->value_ignored) continue;

		/* Add power */
		power += c->value[g->fight_element];
	}

	/* Check for minimum power */
	if (power < p->min_power) power = p->min_power;

	/* Return power */
	return power;
}

/*
 * Handle refresh phase.
 */
void refresh_phase(game *g)
{
	player *p;
	int n;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Assume refresh to six cards */
	n = 6;

	/* Draw cards until full */
	while (p->stack[LOC_HAND] < n)
	{
		/* Draw a card */
		if (!draw_card(g)) break;
	}
}

/*
 * Check for a legal "end of booster/support" phase.
 *
 * Opponent cards can prevent ending this phase until certain conditions
 * are met.
 */
int check_end_support(game *g)
{
	player *p, *opp;
	card *c, *d;
	int i, j, count;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Check for forced retreat effects */
		if (c->d_ptr->special_cat != 7) continue;

		/* Skip ignored texts */
		if (c->text_ignored) continue;

		/* Skip satisfied cards */
		if (c->used) continue;

		/* Check for "or retreat" */
		if (c->d_ptr->special_effect & S7_OR_RETREAT) return 0;
	}

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Check for "you may not" effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Skip ignored texts */
		if (c->text_ignored) continue;

		/* Check for "you may not have more than x characters" */
		if (c->d_ptr->special_effect != (S3_YOU_MAY_NOT | S3_MORE_THAN |
		                                 S3_CHARACTER)) continue;

		/* Assume no active characters */
		count = 0;

		/* Loop over our cards */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			d = &p->deck[j];

			/* Skip inactive cards */
			if (!d->active) continue;

			/* Skip non-character cards */
			if (d->type != TYPE_CHARACTER) continue;

			/* Increase count */
			count++;
		}

		/* Check for too many */
		if (count > c->d_ptr->special_value) return 0;
	}

	/* No cards prevent continuing */
	return 1;
}

/*
 * Handle "end of booster/support" phase effects.
 */
void end_support(game *g)
{
	player *p;
	card *c;
	int i;

	/* Check for unsatisfied dragon attraction */
	check_unsatisfied_attract(g, g->turn);

	/* Check for game over */
	if (g->game_over) return;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Loop through cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Check for "end-of-turn" special effects (category 4) */
		if (c->d_ptr->special_cat != 4) continue;

		/* Skip ignored texts */
		if (c->text_ignored) continue;

		/* Handle effect */
		handle_effect_4(g, c, TIME_ENDSUPPORT);
	}

	/* Loop through our cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip unlanded ships */
		if (!c->landed) continue;

		/* Discard landed ship */
		move_card(g, g->turn, c->d_ptr, LOC_DISCARD, 1);

		/* Ship is no longer landed */
		c->landed = 0;
	}

	/* Check special text targets (and ask if necessary) */
	check_targets(g, g->turn, 1);

	/* Check opponent's special text targets (and ask if necessary) */
	check_targets(g, !g->turn, 1);
}

/*
 * Announce power.
 */
void announce_power(game *g, int element)
{
	player *p, *opp;
	card *c;
	char msg[1024];
	int i, effect, bluff = 0;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Check for fight not started */
	if (!g->fight_started)
	{
		/* Start fight */
		g->fight_started = 1;

		/* Set element */
		g->fight_element = element;

		/* Notice card effects */
		notice_effect_1(g);
	}

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, g->fight_element ?
		              _("%s announces %d earth.\n") :
		              _("%s announces %d fire.\n"),
		        _(p->p_ptr->name), compute_power(g, g->turn));

		/* Send message */
		message_add(msg);
	}

	/* Don't bother with calling bluffs in simulated games */
	if (g->simulation) return;

	/* Look for bluff cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip non-bluff cards */
		if (!c->bluff) continue;

		/* Skip ignored cards */
		if (c->value_ignored) continue;

		/* There is a bluff card */
		bluff = 1;
	}

	/* No bluff to call */
	if (!bluff) return;

	/* Look for "you may not call bluff" effect */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip cards with text ignored */
		if (c->text_ignored) continue;

		/* Skip cards without category 3 effects */
		if (c->d_ptr->special_cat != 3) continue;

		/* Get effect code */
		effect = c->d_ptr->special_effect;

		/* Check for "you may not call bluff" */
		if (effect == (S3_YOU_MAY_NOT | S3_CALL_BLUFF)) return;
	}

	/* Ask opponent to call bluff */
	if (opp->control->call_bluff(g)) bluff_called(g);

	/* Assume unpredictable event occured */
	g->random_event = 1;
}

/*
 * Perform end-of-turn effects.
 */
void end_turn(game *g)
{
	player *p;
	card *c;
	int i, flood = 0;
	char msg[1024];

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Loop through cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Card was no longer played recently */
		c->recent = 0;

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Deactivate leadership cards */
		if (c->where == LOC_LEADERSHIP)
		{
			/* Deactivate card */
			deactivate_card(c);

			/* Handle category 1 effects */
			notice_effect_1(g);
		}

		/* Check for "end-of-turn" special effects */
		if (c->d_ptr->special_time != TIME_ENDTURN) continue;

		/* Skip ignored texts */
		if (c->text_ignored) continue;

		/* Handle effect */
		if (c->d_ptr->special_cat == 4)
		{
			/* Handle moving cards */
			handle_effect_4(g, c, TIME_ENDTURN);
		}
		else if (c->d_ptr->special_cat == 7)
		{
			/* Check for flood */
			if (c->d_ptr->special_effect & S7_FLOOD) flood++;
		}
		else if (c->d_ptr->special_cat == 8)
		{
			/* Handle forcing opponent */
			handle_effect_8(g, c, TIME_ENDTURN);
		}
	}

	/* Check for multiple active floods */
	if (flood > 1)
	{
		/* End current player's turn */
		p->phase = PHASE_NONE;

		/* Switch turn */
		g->turn = !g->turn;

		/* Check for simulation */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg,
			        _("Forcing %s to retreat due to Floods.\n"),
			        _(g->p[g->turn].p_ptr->name));

			/* Send message */
			message_add(msg);
		}

		/* Force retreat */
		retreat(g);
	}
}
