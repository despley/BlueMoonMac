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
#include "net.h"

extern int verbose;


/* #define DEBUG */


/*
 * Action types.
 */
#define ACT_NONE        0
#define ACT_RETREAT     1
#define ACT_RETRIEVE    2
#define ACT_PLAY        3
#define ACT_PLAY_NO     4
#define ACT_ANN_FIRE    5
#define ACT_ANN_EARTH   6
#define ACT_USE         7
#define ACT_SATISFY     8
#define ACT_CHOOSE      9
#define ACT_LAND        10
#define ACT_LOAD        11
#define ACT_BLUFF       12
#define ACT_REVEAL      13

/*
 * An action to take.
 */
typedef struct action
{
	/* Action code */
	int act;

	/* Action argument (card to play, usually) */
	design *arg;

	/* Target argument (ship to be loaded on) */
	design *target;

	/* Cards chosen (when act is ACT_CHOOSE) */
	int chosen;

	/* Index of card used (to reduce AI tree searching) */
	int index;

} action;

/*
 * Maximum action path length.
 */
#define MAX_ACTION 50

/*
 * Current best path.
 */
static action best_path[MAX_ACTION];

#ifdef DEBUG
/* Actions in current evaluated path */
static action cur_path[MAX_ACTION];
#endif

/*
 * Current best path position.
 */
static int best_path_pos;

/*
 * Current best path score.
 */
static double best_path_score;

/*
 * Information about choice to make.
 */
typedef struct node
{
	/* Callback */
	choose_result callback;

	/* Legal combinations */
	int legal[5000];

	/* Number of combinations */
	int num_legal;

	/* Card choices */
	design *choices[DECK_SIZE];

	/* Whose cards are chosen */
	int who;

	/* Data to pass to callback */
	void *data;

} node;

/*
 * Choices to make.
 */
static node nodes[10];

/*
 * Current choice.
 */
static int node_pos;

/*
 * Number of upcoming choices.
 */
static int node_len;

/*
 * Prevent recursive chooses.
 */
static int inside_choose;

/*
 * String used for AI assist purposes.
 */
static char *assist_str;

/*
 * Flag used when checking for no option but retreat.
 */
static int must_retreat;
static int checking_retreat;

/*
 * Flag used when checking for opponent's response to declined fight.
 */
static int checking_decline;


/*
 * A neural net for each player.
 */
net learner[2];

/* Neural net inputs */
#define NET_INPUT 443

/* Number of hidden nodes */
#define HIDDEN_NODES 50

/*
 * Set an input value of the neural net.
 */
#define SET_INPUT(l, n, x) (l)->input_value[(n)] = (x)

/*
 * Copy a game structure and set the "simulation" flag.
 *
 * If a game is a simulation, certain actions like drawing cards are
 * faked in such a way as to not give the AI unfair information.
 */
static void simulate_game(game *sim, game *orig)
{
	/* Copy game */
	memcpy(sim, orig, sizeof(game));

	/* Check for original game */
	if (!sim->simulation)
	{
		/* Set simulation flag */
		sim->simulation = 1;

		/* Remember who initiated simulation */
		sim->sim_turn = sim->turn;

		/* Set both players control interfaces to AI */
		sim->p[0].control = &ai_func;
		sim->p[1].control = &ai_func;

		/* Reset random seed */
		sim->random_seed = 0;
	}
}

/*
 * Evaluate the current game state.
 */
static double eval_game(game *g, int who)
{
	player *p;
	card *c;
	int n = 0, i, j;
	int power, stack, bluff, bad_bluff;
	net *l;

	/* Get player's network */
	l = &learner[who];

	/* Check for no learner loaded */
	if (!l->num_inputs) return 0.5;

	/* Loop over each player */
	for (i = 0; i < 2; i++)
	{
		/* Get player pointer */
		p = &g->p[i];

		/* Loop over cards in deck */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Set input for active cards (except leadership) */
			SET_INPUT(l, n++, c->active && !c->random_fake &&
			                  c->d_ptr->type != TYPE_LEADERSHIP &&
			                  (who == i || c->loc_known));
		}

		/* Loop over cards in deck */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Set input for cards in hand (if known) */
			SET_INPUT(l, n++, (who == i || c->loc_known) &&
			                  c->where == LOC_HAND &&
			                  !c->random_fake);
		}

		/* Loop over cards in deck */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Set input for used cards */
			SET_INPUT(l, n++, (who == i || c->loc_known) &&
			                  !c->random_fake &&
			                  (c->where == LOC_DISCARD ||
			                   c->where == LOC_LEADERSHIP ||
					   (c->where == LOC_COMBAT &&
			                    !c->active)));
		}

		/* Loop over cards in deck */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Set input for cards loaded on ship */
			SET_INPUT(l, n++, (c->ship != NULL));
		}
	}

	/* Get evaluating player */
	p = &g->p[who];

	/* Loop over cards in deck */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Set input for "special" card */
		SET_INPUT(l, n++, c->text_boosted || c->on_bottom || c->bluff);
	}

	/* Assume no bad bluff */
	bad_bluff = 0;

	/* Loop over cards in deck */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip non-bluff cards */
		if (!c->bluff) continue;

		/* Check for bad bluff */
		if ((!g->fight_element && !(c->icons & ICON_BLUFF_F)) ||
		     (g->fight_element && !(c->icons & ICON_BLUFF_E)))
		{
			/* Bluff is bad */
			bad_bluff = 1;
		}
	}

	/* Set input for a bad bluff */
	SET_INPUT(l, n++, bad_bluff);

	/* Set input for game over */
	SET_INPUT(l, n++, g->game_over);

	/* Set input for fight started */
	SET_INPUT(l, n++, g->fight_started);

	/* Set inputs for fight element (only if started) */
	SET_INPUT(l, n++, g->fight_element && g->fight_started);
	SET_INPUT(l, n++, !g->fight_element && g->fight_started);

	/* Loop over players */
	for (i = 0; i < 2; i++)
	{
		/* Get player pointer */
		p = &g->p[i];

		/* Set input if it is this player's turn */
		SET_INPUT(l, n++, g->turn == i && !g->game_over);

		/* Check for fight started */
		if (g->fight_started)
		{
			/* Compute power level */
			power = compute_power(g, i);
		}
		else
		{
			/* Assume no power */
			power = 0;
		}

		/* Loop over possible power values */
		for (j = 0; j < 15; j++)
		{
			/* Set input if player has this much power */
			SET_INPUT(l, n++, power > j);
		}

		/* Count active cards */
		stack = p->stack[LOC_COMBAT] + p->stack[LOC_SUPPORT];

		/* Assume no bluff cards */
		bluff = 0;

		/* Loop over cards */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Check for bluff card */
			if (c->bluff)
			{
				/* Bluff cards do not count for dragons */
				stack--;

				/* Count bluffs */
				bluff++;
			}
		}

		/* Loop over stack sizes */
		for (j = 0; j < 8; j++)
		{
			/* Set input if player has this many cards played */
			SET_INPUT(l, n++, stack > j);
		}

		/* Loop over bluff counts */
		for (j = 0; j < 4; j++)
		{
			/* Set input if player has this many bluffs */
			SET_INPUT(l, n++, bluff > j);
		}

		/* Count cards in hand */
		stack = p->stack[LOC_HAND];

		/* Loop over hand sizes */
		for (j = 0; j < 10; j++)
		{
			/* Set input if player has this many cards */
			SET_INPUT(l, n++, stack > j);
		}

		/* Count cards in draw deck and hand */
		stack = p->stack[LOC_DRAW] + p->stack[LOC_HAND];

		/* Loop over deck sizes */
		for (j = 0; j < 30; j++)
		{
			/* Set input if player has this many cards */
			SET_INPUT(l, n++, stack > j);
		}

		/* Assume no characters */
		stack = 0;

		/* Loop over deck */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Skip non-characters */
			if (c->d_ptr->type != TYPE_CHARACTER) continue;

			/* Skip cards not in hand */
			if (c->where == LOC_HAND &&
			    (who == i || c->loc_known) &&
			    !c->random_fake)
			{
				/* Add more character */
				stack++;
			}
		}

		/* Loop over character counts */
		for (j = 0; j < 5; j++)
		{
			/* Set input if player has this many characters */
			SET_INPUT(l, n++, stack > j);
		}

		/* Assume no undisclosed cards */
		stack = 0;

		/* Loop over deck */
		for (j = 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			c = &p->deck[j];

			/* Skip cards not in hand */
			if (c->where != LOC_HAND) continue;

			/* Skip disclosed cards */
			if (c->disclosed) continue;

			/* Count undisclosed cards */
			stack++;
		}

		/* Loop over disclose counts */
		for (j = 0; j < 6; j++)
		{
			/* Set input if player has this cards disclosed */
			SET_INPUT(l, n++, stack > j);
		}

		/* Set input if player is first to run out of cards */
		SET_INPUT(l, n++, p->no_cards);

		/* Loop over dragon counts */
		for (j = 0; j < 3; j++)
		{
			/* Set input if player has this many dragons */
			SET_INPUT(l, n++, p->dragons > j);
		}

		/* Set input if player has instant victory */
		SET_INPUT(l, n++, p->instant_win);
	}

	/* Compute network value */
	compute_net(l);

#ifdef DEBUG
	/* Print score and path to get here */
	if (verbose && !checking_retreat && best_path_pos > 0)
	{
		printf("%.12lf: ", l->win_prob[who]);

		for (i = 0; i <= best_path_pos; i++)
		{
			action a;

			a = cur_path[i];

			switch (a.act)
			{
				case ACT_NONE: break;
				case ACT_RETREAT: printf("Retreat "); break;
				case ACT_RETRIEVE: printf("Retrieve %s ", a.arg->name); break;
				case ACT_PLAY: printf("Play %s ", a.arg->name); break;
				case ACT_PLAY_NO: printf("Play (no) %s ", a.arg->name); break;
				case ACT_ANN_FIRE: printf("Announce fire "); break;
				case ACT_ANN_EARTH: printf("Announce earth "); break;
				case ACT_USE: printf("Use %s ", a.arg->name); break;
				case ACT_SATISFY: printf("Satsify %s ", a.arg->name); break;
				case ACT_CHOOSE: printf("Choose %d ", a.chosen); break;
				case ACT_LAND: printf("Land %s ", a.arg->name); break;
				case ACT_LOAD: printf("Load %s on %s ", a.arg->name, a.target->name); break;
				case ACT_BLUFF: printf("Bluff %s ", a.arg->name); break;
				case ACT_REVEAL: printf("Reveal %s ", a.arg->name); break;
			}
		}

		if (must_retreat) printf("Force retreat");

		if (checking_decline)
		{
			printf("Responding %s", g->fight_element ? "earth" : "fire");
		}
	
		printf("\n");
	}
#endif

	/* Return output */
	return l->win_prob[who];
}

/*
 * Perform a training iteration.
 *
 * The "desired" array is passed only at the end of the game.  Otherwise
 * we train the network to give results (using past inputs) to be like our
 * results using current inputs.
 */
static void perform_training(game *g, int who, double *desired)
{
	double target[2];
	double lambda = 1.0;
	int i;
	net *l;

	/* Get correct network to train */
	l = &learner[who];

	/* Check for uninitialized network */
	if (!l->num_inputs) return;

	/* Get current state */
	eval_game(g, who);

	/* Store current inputs */
	store_net(l);

	/* Check for passed in results */
	if (desired)
	{
		/* Copy results */
		target[0] = desired[0];
		target[1] = desired[1];

		/* Train current inputs with desired outputs */
		train_net(l, lambda, target);
	}
	else
	{
		/* Copy results */
		target[0] = l->win_prob[0];
		target[1] = l->win_prob[1];
	}

	/* Loop over past inputs (starting with most recent) */
	for (i = l->num_past - 2; i >= 0; i--)
	{
		/* Copy past inputs to network */
		memcpy(l->input_value, l->past_input[i],
		       sizeof(int) * (l->num_inputs + 1));

		/* Compute net */
		compute_net(l);

		/* Train using this */
		train_net(l, lambda, target);

		/* Reduce training amount for less recent results */
		lambda *= 0.9;
	}
}

/*
 * Initialize AI.
 */
static void ai_initialize(game *g, int who)
{
	char fname[1024], buf[1024];

	/* Create neural net */
	make_learner(&learner[who], NET_INPUT, HIDDEN_NODES, 2);

	/* Set learning rate */
	learner[who].alpha = 0.0001;
	/* learner[who].alpha = 0.0; printf("WARNING: alpha is 0\n"); */

	/* Create network filename */
	sprintf(fname, DATADIR "/networks/bluemoon.net.%s.%s",
	                                     g->p[who].p_ptr->name,
	                                     g->p[!who].p_ptr->name);

	/* Attempt to load net weights from disk */
	if (load_net(&learner[who], fname))
	{
		/* Create warning message */
		sprintf(buf,
		        _("WARNING: Couldn't open %s, expect random play!\n"),
		        fname);

		/* Send message */
		message_add(buf);
	}

	/* Evaluate starting position */
	eval_game(g, who);

	/* Message */
	if (verbose >= 1)
	{
		/* Print win probabilities of starting state */
		printf("%s Start prob: %f %f\n", g->p[who].p_ptr->name,
						 learner[who].win_prob[0],
						 learner[who].win_prob[1]);
	}
}

/*
 * Return how many more bluff cards we can play before it is obvious that
 * it is bad.
 *
 * Return a negative number if we are already at or past that point.
 */
static int check_auto_bluff(game *g, int future)
{
	player *p;
	card *c;
	int i;
	int bluff = 0, unknown_f = 0, unknown_e = 0;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Check for uncallable bluff */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards without category 3 special text */
		if (c->d_ptr->special_cat != 3) continue;

		/* Check for "you may not call bluff" text */
		if (c->d_ptr->special_effect !=(S3_YOU_MAY_NOT | S3_CALL_BLUFF))
		{
			continue;
		}

		if (c->active && !c->text_ignored) return 99;

		if (c->where == LOC_HAND && future) return 99;
	}

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Check for fire bluff icon */
		if (c->d_ptr->icons & ICON_BLUFF_F)
		{
			/* Count cards with unknown locations */
			if (!c->loc_known) unknown_f++;
		}

		/* Check for earth bluff icon */
		if (c->d_ptr->icons & ICON_BLUFF_E)
		{
			/* Count cards with unknown locations */
			if (!c->loc_known) unknown_e++;
		}

		/* Count bluff cards */
		if (c->bluff) bluff++;
	}

	/* Check for fight started */
	if (g->fight_started)
	{
		/* Return number of excess unknown bluff cards */
		return g->fight_element ? unknown_e - bluff : unknown_f - bluff;
	}

	/* Return number of excess unknown bluff cards in lesser element */
	return unknown_e < unknown_f ? unknown_e - bluff : unknown_f - bluff;
}

/*
 * Add a list of legal support phase actions to the given list.
 *
 * This includes loading ships and playing bluff cards.
 */
static int legal_support(game *g, action *legal, int n)
{
	player *p;
	card *c, *d;
	int i, j;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Check for no support actions allowed */
	if (!support_allowed(g)) return n;

	/* Look for played ships */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Don't load ships when checking forced retreat */
		if (checking_retreat) break;

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Skip non-ship cards */
		if (!c->d_ptr->capacity) continue;

		/* Loop over other cards */
		for (j = p->last_played + 1; j < DECK_SIZE; j++)
		{
			/* Get card pointer */
			d = &p->deck[j];

			/* Skip ineligible cards */
			if (!card_eligible(g, d->d_ptr)) continue;

			/* Skip cards that aren't character/support/booster */
			if (d->d_ptr->type != TYPE_CHARACTER &&
			    d->d_ptr->type != TYPE_BOOSTER &&
			    d->d_ptr->type != TYPE_SUPPORT) continue;

			/* Check for legal loading */
			if (load_allowed(g, c->d_ptr))
			{
				/* Add load action */
				legal[n].act = ACT_LOAD;
				legal[n].index = j;
				legal[n].arg = d->d_ptr;
				legal[n++].target = c->d_ptr;
			}
		}

		/* XXX Stop after first ship with space */
		break;
	}

	/* Check for bluffs illegal */
	if (!bluff_legal(g, g->turn)) return n;

	/* Check for no more unknown bluff cards available */
	if (check_auto_bluff(g, 1) < 1) return n;

	/* Look for bluff cards */
	for (i = p->last_played + 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip ineligible cards */
		if (!card_eligible(g, c->d_ptr)) continue;

		/* Skip cards without bluff icons */
		if (!(c->icons & ICON_BLUFF_MASK)) continue;

		/* Add bluff action */
		legal[n].act = ACT_BLUFF;
		legal[n].index = i;
		legal[n++].arg = c->d_ptr;

		/* Any bluff is as good as another when checking retreat */
		if (checking_retreat) break;
	}

	/* Return length of list */
	return n;
}

/*
 * Return a list of legal actions from the given game state.
 *
 * "ACT_NONE" means to take no action and advance the phase counter.
 */
static int legal_act(game *g, action *legal)
{
	player *p, *opp;
	card *c;
	int power;
	int i, n = 0;
	
	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Switch on phase */
	switch (p->phase)
	{
		/* Beginning of turn */
		case PHASE_BEGIN:
		{
			/* Loop over cards */
			for (i = p->last_played + 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip inactive cards */
				if (!c->active) continue;

				/* Skip cards that aren't ships */
				if (!c->d_ptr->capacity) continue;

				/* Skip cards that are already landed */
				if (c->landed) continue;

				/* Add "land" action */
				legal[n].act = ACT_LAND;
				legal[n].index = i;
				legal[n++].arg = c->d_ptr;
			}

			/* Loop over cards */
			for (i = p->last_played + 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip non-bluff cards */
				if (!c->bluff) continue;

				/* Don't reveal when checking retreat */
				if (!checking_retreat)
				{
					/* Add "reveal" action */
					legal[n].act = ACT_REVEAL;
					legal[n].index = i;
					legal[n++].arg = c->d_ptr;
				}
			}

			/* Check for active cards that can be retrieved */
			for (i = p->last_played + 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip unretrieveable */
				if (!retrieve_legal(g, c)) continue;

				/* Add retrieve action */
				legal[n].act = ACT_RETRIEVE;
				legal[n].index = i;
				legal[n++].arg = c->d_ptr;
			}

			/* No action is always allowed */
			legal[n++].act = ACT_NONE;

			/* Done */
			return n;
		}

		/* Play a card */
		case PHASE_LEADER:
		case PHASE_CHAR:
		case PHASE_SUPPORT:
		{
			if (check_auto_bluff(g, 1) < 0) return 0;

			/* Loop over active cards */
			for (i = 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip inactive cards */
				if (!c->active) continue;

				/* Skip cards with no special effect */
				if (!c->d_ptr->special_cat) continue;

				/* Skip cards with ignored text */
				if (c->text_ignored) continue;

				/* Skip cards already used */
				if (c->used) continue;

				/* Skip cards that can't be used anytime */
				if (c->d_ptr->special_time != TIME_MYTURN)
					continue;

				/* Skip cards with no useful effect */
				if (!special_possible(g, c->d_ptr)) continue;

				/* Add action to use card power */
				legal[n].act = ACT_USE;
				legal[n++].arg = c->d_ptr;
			}

			/* Always use special text first if possible */
			if (n) return n;

			/* Look for cards to play */
			for (i = p->last_played + 1; i < DECK_SIZE; i++)
			{
				/* Get card pointer */
				c = &p->deck[i];

				/* Skip randomly chosen cards */
				if (c->random_fake) continue;

				/* Skip ineligible cards */
				if (!card_eligible(g, c->d_ptr)) continue;

				/* Check for illegal leadership card */
				if (c->d_ptr->type == TYPE_LEADERSHIP &&
				    p->phase != PHASE_LEADER) continue;

				/* Check for illegal character card */
				if (c->d_ptr->type == TYPE_CHARACTER &&
				    p->phase != PHASE_CHAR) continue;

				/* Check for illegal booster card */
				if (c->d_ptr->type == TYPE_BOOSTER &&
				    p->phase != PHASE_SUPPORT) continue;

				/* Check for illegal support card */
				if (c->d_ptr->type == TYPE_SUPPORT &&
				    p->phase != PHASE_SUPPORT) continue;

				/* Check card legality */
				if (!card_allowed(g, c->d_ptr)) continue;

				/* Check for optional special effect */
				if (((c->d_ptr->special_cat == 4 &&
				      c->d_ptr->special_effect & S4_OPTIONAL) ||
				     (c->d_ptr->special_cat == 8 &&
				      c->d_ptr->special_effect & S8_OPTIONAL))&&
				    !checking_retreat)
				{
					/* Playing card without effect */
					legal[n].act = ACT_PLAY_NO;
					legal[n].index = i;
					legal[n++].arg = c->d_ptr;
				}

				/* Playing card is allowed */
				legal[n].act = ACT_PLAY;
				legal[n].index = i;
				legal[n++].arg = c->d_ptr;

				/* XXX Always play characters on ship */
				if (p->phase == PHASE_CHAR && c->ship) return n;
			}

			/* Check for support phase */
			if (p->phase == PHASE_SUPPORT)
			{
				/* Add support actions */
				n = legal_support(g, legal, n);
			}

			/* Check every action when checking retreat */
			if (checking_retreat && n > 0) break;

			/* Advance phase allowed unless character is unplayed */
			if (p->phase != PHASE_CHAR || p->char_played)
			{
				/* Add no action */
				legal[n++].act = ACT_NONE;
			}

			/* Done, add special actions */
			break;
		}

		/* Retreat phase */
		case PHASE_RETREAT:
		{
			/* Retreat is always allowed */
			legal[n++].act = ACT_RETREAT;

			/* Not retreating is allowed as well */
			legal[n++].act = ACT_NONE;

			/* Done */
			return n;
		}

		/* Announce power */
		case PHASE_ANNOUNCE:
		{
			/* Check for no fight */
			if (!g->fight_started)
			{
				/* Both elements are allowed */
				legal[n++].act = ACT_ANN_FIRE;
				legal[n++].act = ACT_ANN_EARTH;
			}
			else
			{
				/* Get opponent's power */
				power = compute_power(g, !g->turn);

				/* Loop over our combat cards */
				for (i = 1; i < DECK_SIZE; i++)
				{
					/* Get card pointer */
					c = &p->deck[i];

					/* Skip non-combat cards */
					if (c->where != LOC_COMBAT) continue;

					/* Skip inactive cards */
					if (!c->active) continue;

					/* Look for shield in fight element */
					if (c->icons & (1 << g->fight_element))
					{
						/* Opponent has no power */
						power = 0;
					}
				}

				/* Check for enough power */
				if (power <= compute_power(g, g->turn))
				{
					/* We can announce power */
					legal[n++].act = ACT_ANN_FIRE +
					                 g->fight_element;
				}
			}

			/* Done */
			return n;
		}

		/* Ensure opponent cards are satisfied */
		case PHASE_AFTER_SB:
		{
			/* Check for automatic bluff call */
			if (check_auto_bluff(g, 0) < 0) return n;

			/* Check for legal end of support phase */
			if (check_end_support(g))
			{
				/* Add advance phase action */
				legal[n++].act = ACT_NONE;
			}

			/* Done */
			return n;
		}

		/* Phases where we can do nothing */
		case PHASE_START:
		case PHASE_REFRESH:
		case PHASE_END:
		case PHASE_OVER:
		{
			/* Only legal action is to do nothing */
			legal[n++].act = ACT_NONE;

			/* Done */
			return n;
		}
	}

	/* Do not allow further actions if none are available so far */
	if (!n) return n;

	/* No need to satisfy "discard or..." cards in character phase */
	if (p->phase == PHASE_CHAR) return n;

	/* Check for active opponent "discard or..." cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip non-active cards */
		if (!c->active) continue;

		/* Skip cards with text ignored */
		if (c->text_ignored) continue;

		/* Skip non-category 7 cards */
		if (c->d_ptr->special_cat != 7) continue;

		/* Skip non-discard cards */
		if (!(c->d_ptr->special_effect & S7_DISCARD_MASK)) continue;

		/* Skip satisfied cards */
		if (c->used) continue;

		/* Check for satisfaction impossible */
		if (!satisfy_possible(g, c->d_ptr)) continue;

		/* Add action to satisfy card */
		legal[n].act = ACT_SATISFY;
		legal[n++].arg = c->d_ptr;

		/* Stop looking for further cards to satisfy */
		return n;
	}

	/* Return number of legal actions */
	return n;
}

/*
 * Perform the given action.
 */
static void perform_act(game *g, action a)
{
	player *p, *opp;
	int old_phase;

	/* Get player pointers */
	p = &g->p[g->turn];
	opp = &g->p[!g->turn];

	/* Remember current phase */
	old_phase = p->phase;

	/* Switch on action */
	switch (a.act)
	{
		/* No action, advance phase counter */
		case ACT_NONE:
		{
			/* Advance phase counter */
			p->phase++;

			/* Take care of bookkeeping */
			switch (old_phase)
			{
				/* Start of turn */
				case PHASE_START:

					/* Start turn */
					start_turn(g);

					/* Done */
					break;

				/* After support/booster */
				case PHASE_AFTER_SB:

					/* End support phase */
					end_support(g);
					
					/* Done */
					break;

				/* Refresh hand */
				case PHASE_REFRESH:

					/* Refresh */
					refresh_phase(g);

					/* Done */
					break;

				/* End of turn */
				case PHASE_END:

					/* End current turn */
					end_turn(g);

					/* Done */
					break;

				/* Move to next player */
				case PHASE_OVER:

					/* Player is done */
					p->phase = PHASE_NONE;

					/* Next player */
					g->turn = !g->turn;

					/* Start opponent's turn */
					opp->phase = PHASE_START;

					/* Done (completely) */
					return;
			}

			/* Clear last played card */
			p->last_played = 0;

			/* Done */
			break;
		}

		/* Retreat */
		case ACT_RETREAT:
		{
			/* Retreat */
			retreat(g);

			/* Done */
			break;
		}

		/* Retrieve a card */
		case ACT_RETRIEVE:
		{
			/* Set last played */
			p->last_played = a.index;

			/* Retrieve card */
			retrieve_card(g, a.arg);

			/* Done */
			break;
		}

		/* Play a card */
		case ACT_PLAY:
		{
			/* Set last played */
			p->last_played = a.index;

			/* Play card */
			play_card(g, a.arg, 0, 0);

			/* Done */
			break;
		}

		/* Play a card without special effect */
		case ACT_PLAY_NO:
		{
			/* Set last played */
			p->last_played = a.index;

			/* Play card */
			play_card(g, a.arg, 1, 0);

			/* Done */
			break;
		}

		/* Announce power */
		case ACT_ANN_FIRE:
		case ACT_ANN_EARTH:
		{
			/* Increment phase counter */
			p->phase++;

			/* Announce power */
			announce_power(g, a.act - ACT_ANN_FIRE);

			/* Done */
			break;
		}

		/* Use card special power */
		case ACT_USE:
		{
			/* Use card */
			use_special(g, a.arg);

			/* Done */
			break;
		}

		/* Satisfy opponent's "discard" card */
		case ACT_SATISFY:
		{
			/* Satisfy requirement */
			satisfy_discard(g, a.arg);

			/* Done */
			break;
		}

		/* Land a ship */
		case ACT_LAND:
		{
			/* Set last played */
			p->last_played = a.index;

			/* Land ship */
			land_ship(g, a.arg);

			/* Done */
			break;
		}

		/* Load a card onto a ship */
		case ACT_LOAD:
		{
			/* Set last played */
			p->last_played = a.index;

			/* Load card */
			load_card(g, a.arg, a.target);

			/* Done */
			break;
		}

		/* Play a bluff card */
		case ACT_BLUFF:
		{
			/* Set last played */
			p->last_played = a.index;

			/* Play bluff */
			play_bluff(g, a.arg);

			/* Done */
			break;
		}

		/* Reveal a bluff card */
		case ACT_REVEAL:
		{
			/* Set last played */
			p->last_played = a.index;

			/* Reveal bluff */
			reveal_bluff(g, g->turn, a.arg);

			/* Done */
			break;
		}
	}
}

/* Foward declaration */
static double find_action(game *g);

/*
 * Check if current player must retreat.
 *
 * We run this check from the point of view of the opponent.
 *
 * If current player must retreat, simulate the results.
 */
static void check_retreat(game *g)
{
	game sim;
	player *p, *opp;
	card *c;
	int i;
	int all_known = 1, moved = 0, bluff = 0;

	/* Do nothing if no fight to retreat from */
	if (!g->fight_started) return;

	/* Get opponent pointer */
	opp = &g->p[!g->turn];

	/* Loop over opponent cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Skip inactive cards */
		if (!c->active) continue;

		/* Check for bluff card */
		if (c->bluff) bluff = 1;

		/* Skip cards with text ignored */
		if (c->text_ignored) continue;

		/* Skip cards without category 3 special text */
		if (c->d_ptr->special_cat != 3) continue;

		/* Skip cards that don't disallow calling bluff */
		if (c->d_ptr->special_effect !=(S3_YOU_MAY_NOT | S3_CALL_BLUFF))
			continue;

		/* Assume no bluff cards are in play */
		bluff = 0;
		break;
	}

	/* Do not check for forced retreat if bluff may be called */
	if (bluff) return;

	/* Simulate game */
	simulate_game(&sim, g);

	/* Get player pointer */
	p = &sim.p[sim.turn];

	/* Loop over cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &p->deck[i];

		/* Skip cards not in hand */
		if (c->where != LOC_HAND) continue;

		/* Check for unknown card */
		if (!c->loc_known) all_known = 0;
	}

	/* Check for not all cards in hand known */
	if (!all_known)
	{
		/* Pretend all unknown cards are in hand */
		for (i = 1; i < DECK_SIZE; i++)
		{
			/* Get card pointer */
			c = &p->deck[i];

			/* Skip cards with known locations */
			if (c->loc_known && !c->random_fake) continue;

			/* Clear "random" flag */
			c->random_fake = 0;

			/* Move card to hand */
			c->where = LOC_HAND;

			/* Count cards moved */
			moved++;
		}
	}

	/* XXX Do nothing if most cards moved */
	if (moved > 15) return;

	/* Set retreat flag */
	must_retreat = 1;
	checking_retreat = 1;

	/* Simulate possible actions */
	find_action(&sim);

	/* Check for retreat flag still set */
	if (must_retreat)
	{
		/* Force current player to retreat before evaluating score */
		retreat(g);
	}

	/* Clear retreat check flag */
	checking_retreat = 0;
}

/*
 * Simulate the results of declining a fight.
 *
 * Here we assume that the opponent will start a fight.  We check both
 * elements, and assume the worst will be chosen.
 */
static double check_decline(game *g, int who)
{
	game sim;
	player *opp = &g->p[who];
	double score, b_s;

	/* Get score of current situation */
	b_s = eval_game(g, who);

	/* Check for no response possible from opponent */
	if (opp->stack[LOC_HAND] == 0) return b_s;

	/* Set checking flag */
	checking_decline = 1;

	/* Simulate game */
	simulate_game(&sim, g);

	/* Simulate fight started in fire */
	sim.fight_started = 1;
	sim.fight_element = 0;
	sim.turn = who;

	/* Get score */
	score = eval_game(&sim, who);

	/* Check for worse */
	if (score < b_s) b_s = score;

	/* Simulate game */
	simulate_game(&sim, g);

	/* Simulate fight started in earth */
	sim.fight_started = 1;
	sim.fight_element = 1;
	sim.turn = who;

	/* Get score */
	score = eval_game(&sim, who);

	/* Check for worse */
	if (score < b_s) b_s = score;

	/* Clear checking flag */
	checking_decline = 0;

	/* Return worst case */
	return b_s;
}

/*
 * Handle a choice to be made.
 */
static double choose_action(game *g)
{
	game sim;
	design *list[DECK_SIZE], **choices;
	node *n_ptr;
	void *data;
	double score, b_s = -1;
	int old_turn;
	int best_combo = 0;
	int i, j, num_chosen;

	/* Get current player's turn */
	old_turn = g->turn;

	/* Get pointer to choice node */
	n_ptr = &nodes[node_pos];

	/* Track current choice node */
	node_pos++;
	best_path_pos++;

	/* Loop over choices */
	for (i = 0; i < n_ptr->num_legal; i++)
	{
		/* Avoid unnecessary work when checking for forced retreat */
		if (checking_retreat && !must_retreat) break;

		/* Clear number chosen */
		num_chosen = 0;

		/* Get choice list */
		choices = n_ptr->choices;

		/* Get callback data */
		data = n_ptr->data;

		/* Loop over combination */
		for (j = 0; (1 << j) <= n_ptr->legal[i]; j++)
		{
			/* Check for bit set */
			if (n_ptr->legal[i] & (1 << j))
			{
				/* Add choice to list */
				list[num_chosen++] = choices[j];
			}
		}

#ifdef DEBUG
		/* Remember current path */
		cur_path[best_path_pos].act = ACT_CHOOSE;
		cur_path[best_path_pos].chosen = n_ptr->legal[i];
#endif

		/* Simulate game */
		simulate_game(&sim, g);

		/* Make choice */
		if (!n_ptr->callback(&sim, n_ptr->who, list, num_chosen, data))
		{
			printf("Callback failed!\n");
		}

		/* Check for turn change */
		if (sim.turn != old_turn)
		{
			/* Are we checking forced retreat */
			if (checking_retreat)
			{
				/* Score is unimportant */
				score = 0;
			}
			else
			{
				/* Assume worst-case response from opponent */
				score = check_decline(&sim, sim.sim_turn);
			}
		}
		else
		{
			/* Continue searching */
			score = find_action(&sim);
		}

		/* Check for better score among actions */
		if (score >= b_s)
		{
			/* Remember best */
			b_s = score;
			best_combo = n_ptr->legal[i];
		}
	}

	/* Remove node from list */
	node_pos--;
	node_len--;

	/* Return to current path position */
	best_path_pos--;

	/* Check for better actions than previously discovered */
	if (!checking_retreat && b_s >= best_path_score)
	{
		/* Store action in best path */
		best_path[best_path_pos].act = ACT_CHOOSE;
		best_path[best_path_pos].chosen = best_combo;

		/* Save best score seen */
		best_path_score = b_s;
	}

	/* Return best score */
	return b_s;
}

/*
 * Find the best "action path" available from the given state.
 *
 * We also return the score of the endstate that will result.
 *
 * We return -1 if no legal actions are available.
 *
 * This function is recursive.
 */
static double find_action(game *g)
{
	game sim;
	player *p;
	int old_turn;
	int i, n;
	action legal[MAX_ACTION], best_act;
	double score, b_s = -1;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Get current player's turn */
	old_turn = g->turn;

	/* Check for game over */
	if (g->game_over)
	{
		/* Clear any choice nodes that haven't been examined */
		node_len = node_pos;

		/* Return end of game score */
		return eval_game(g, g->sim_turn);
	}

	/* Check for choice to make */
	if (node_pos < node_len)
	{
		/* Handle choice node instead of normal */
		return choose_action(g);
	}

	/* Avoid needlees work when checking for forced retreat */
	if (checking_retreat && !must_retreat) return 0;

	/* Get legal actions to take */
	n = legal_act(g, legal);

	/* Check for no legal actions */
	if (!n) return -1;

	/* Check for only one possible action */
	if (n == 1)
	{
		/* Increase path position for future searching */
		best_path_pos++;

#ifdef DEBUG
		/* Remember current path */
		cur_path[best_path_pos] = legal[0];
#endif

		/* Perform that action */
		perform_act(g, legal[0]);

		/* Check for turn change */
		if (g->turn != old_turn)
		{
			/* Are we checking opponent's response */
			if (checking_retreat)
			{
				/* Score is unimportant */
				score = 0.0;

				/* We must not be forced to retreat */
				must_retreat = 0;
			}
			else
			{
				/* Check for inevitable retreat from opponent */
				check_retreat(g);

				/* Get score */
				score = eval_game(g, g->sim_turn);

				/* Clear must retreat flag */
				must_retreat = 0;
			}
		}

		/* Continue searching */
		else score = find_action(g);

		/* Return to current path position */
		best_path_pos--;

		/* Check for better actions than previously discovered */
		if (!checking_retreat && score >= best_path_score)
		{
			/* Store action in best path */
			best_path[best_path_pos] = legal[0];

			/* Save best score seen */
			best_path_score = score;
		}

		/* Return score */
		return score;
	}

	/* Increase path position for future searching */
	best_path_pos++;

	/* Loop over available actions */
	for (i = 0; i < n; i++)
	{
#ifdef DEBUG
		/* Remember current path */
		cur_path[best_path_pos] = legal[i];
#endif

		/* Avoid unnecessary work when checking for forced retreat */
		if (checking_retreat && !must_retreat) break;

		/* Copy game */
		simulate_game(&sim, g);

		/* Perform action */
		perform_act(&sim, legal[i]);

		/* Check for retreat */
		if (legal[i].act == ACT_RETREAT && node_pos == node_len)
		{
			/* Are we checking for forced retreat */
			if (checking_retreat)
			{
				/* Score is unimportant */
				score = 0;
			}
			else
			{
				/* Get score */
				score = eval_game(&sim, sim.sim_turn);
			}
		}

		/* Normal action */
		else
		{
			/* Continue searching */
			score = find_action(&sim);
		}

		/* Check for better score among actions */
		if (score >= b_s)
		{
			/* Remember best */
			b_s = score;
			best_act = legal[i];
		}
	}

	/* Return to current path position */
	best_path_pos--;

	/* Check for better actions than previously discovered */
	if (!checking_retreat && b_s >= best_path_score)
	{
		/* Store action in best path */
		best_path[best_path_pos] = best_act;

		/* Save best score seen */
		best_path_score = b_s;
	}

	/* Return best score */
	return b_s;
}

/*
 * Have the AI player take an action.
 */
static void ai_take_action(game *g)
{
	game sim;
	player *p;
	action current;
	int old_turn;

	/* Get player pointer */
	p = &g->p[g->turn];

	/* Save current turn */
	old_turn = g->turn;

	/* Clear best path */
	best_path_pos = 0;
	best_path_score = -1;

	/* Check for beginning of turn */
	if (p->phase == PHASE_START)
	{
		/* Train networks with past inputs */
		perform_training(g, g->turn, NULL);
		perform_training(g, !g->turn, NULL);
	}

	/* Clear random event flag */
	g->random_event = 0;

	/* Check for error in handling choice nodes */
	if (node_len > 0 || node_pos > 0)
	{
		printf("Choice nodes around\n");
	}

	/* Simulate game */
	simulate_game(&sim, g);

#ifdef DEBUG
	printf("START\n");
#endif

	/* Find best action path */
	find_action(&sim);

#ifdef DEBUG
	printf("END\n");
#endif

	/* Start at beginning of path */
	best_path_pos = 0;

	/* Loop until end */
	while (1)
	{
		/* Get current action */
		current = best_path[best_path_pos];

		/* Check for error */
		if (current.act == ACT_CHOOSE)
		{
			printf("Trying to perform choose action!\n");
			break;
		}

		/* Advance to next */
		best_path_pos++;

		/* Perform current action */
		perform_act(g, current);

		/* Check for random event */
		if (g->random_event) break;

		/* Check for retreat */
		if (current.act == ACT_RETREAT) break;

		/* Check for turn change */
		if (g->turn != old_turn) break;

		/* Check for game over */
		if (g->game_over) break;
	}
}

/*
 * Return a string containing the AI's assumption about the best possible
 * move.
 */
void ai_assist(game *g, char *buf)
{
	game sim;
	action current;
	char tmp[1024];

	/* Clear best path */
	best_path_pos = 0;
	best_path_score = -1;

	/* Simulate game */
	simulate_game(&sim, g);

	/* Find best action path */
	find_action(&sim);

	/* Check for no legal moves */
	if (best_path_score == -1)
	{
		/* Add message */
		strcpy(buf, "No legal moves!\n");
		return;
	}

	/* Start back at beginning */
	simulate_game(&sim, g);

	/* Clear buffer */
	strcpy(buf, "");

	/* Use buffer for assist messages */
	assist_str = buf;

	/* Start at beginning of path */
	best_path_pos = 0;

	/* Loop until done */
	while (sim.p[g->turn].phase <= PHASE_ANNOUNCE)
	{
		/* Get current action */
		current = best_path[best_path_pos];

		/* Advance position */
		best_path_pos++;

		/* Check current action */
		switch (current.act)
		{
			/* None */
			case ACT_NONE:

				/* No message */
				strcpy(tmp, "");
				break;

			/* Retreat */
			case ACT_RETREAT:

				/* Add message */
				sprintf(tmp, "Retreat\n");
				break;

			/* Retrieve */
			case ACT_RETRIEVE:

				/* Add message */
				sprintf(tmp, "Retrieve %s\n",
				             current.arg->name);
				break;

			/* Play */
			case ACT_PLAY:

				/* Add message */
				sprintf(tmp, "Play %s\n",
				              current.arg->name);
				break;

			/* Play with no effect */
			case ACT_PLAY_NO:

				/* Add message */
				sprintf(tmp, "Play %s with no effect\n",
				              current.arg->name);
				break;

			/* Announce fire */
			case ACT_ANN_FIRE:

				/* Add message */
				sprintf(tmp, "Announce fire\n");
				break;

			/* Announce earth */
			case ACT_ANN_EARTH:

				/* Add message */
				sprintf(tmp, "Announce earth\n");
				break;

			/* Use special */
			case ACT_USE:

				/* Add message */
				sprintf(tmp, "Use %s special text\n",
				              current.arg->name);
				break;

			/* Satisfy */
			case ACT_SATISFY:

				/* Add message */
				sprintf(tmp, "Satisfy %s\n",
				              current.arg->name);
				break;

			/* Land ship */
			case ACT_LAND:

				/* Add message */
				sprintf(tmp, "Land %s\n",
				              current.arg->name);
				break;

			/* Load ship */
			case ACT_LOAD:

				/* Add message */
				sprintf(tmp, "Load %s onto %s\n",
				              current.arg->name,
				              current.target->name);
				break;

			/* Play bluff */
			case ACT_BLUFF:

				/* Add message */
				sprintf(tmp, "Bluff %s\n",
				              current.arg->name);
				break;

			/* Reveal bluff */
			case ACT_REVEAL:

				/* Add message */
				sprintf(tmp, "Reveal %s\n",
				              current.arg->name);
				break;
		}

		/* Add message to buffer */
		strcat(buf, tmp);

		/* Perform action */
		perform_act(&sim, current);

		/* Check for retreat */
		if (current.act == ACT_RETREAT) break;

		/* Check for game over */
		if (sim.game_over) break;
	}

	/* Clear assist string */
	assist_str = NULL;
}

/*
 * Add message to assist string from chooser.
 */
static void choose_assist(design *chosen[DECK_SIZE], int num_chosen)
{
	char tmp[1024];
	int i;

	/* Start message */
	if (num_chosen)
	{
		/* Format message */
		sprintf(tmp, "Choose %d: ", num_chosen);
	}
	else
	{
		/* Add message */
		strcat(assist_str, "Choose none\n");
		return;
	}

	/* Loop over chosen */
	for (i = 0; i < num_chosen; i++)
	{
		/* Add choice */
		strcat(tmp, chosen[i]->name);

		/* Add spaces */
		if (i + 1 < num_chosen) strcat(tmp, ", ");
	}

	/* Add newline */
	strcat(tmp, "\n");

	/* Add to string */
	strcat(assist_str, tmp);
}

/*
 * Flag to stop searching for legal combinations.
 *
 * Sometimes when checking for opponent forced retreat, we don't care to
 * search for the best combination, just one that works.
 */
static int stop_choose;


/*
 * Card chooser helper function.
 *
 * This function recursively calls itself to check all combinations.
 */
static void ai_choose_aux(game *g, int chooser, int who, design **choices,
                          int n, int c, int chosen, int *best, double *b_s,
                          choose_result callback, void *data)
{
	game sim;
	design *list[DECK_SIZE];
	int i, num_chosen = 0;
	int num_legal;
	int callback_value;
	double score;

	/* Check for no need to look further */
	if (stop_choose) return;

	/* Check for too few choices */
	if (c > n) return;

	/* Check for end */
	if (!n)
	{
		/* Loop over chosen cards */
		for (i = 0; (1 << i) <= chosen; i++)
		{
			/* Check for bit set */
			if (chosen & (1 << i))
			{
				/* Add card to list */
				list[num_chosen++] = choices[i];
			}
		}

		/* Copy game */
		simulate_game(&sim, g);

		/* Apply result */
		callback_value = callback(&sim, who, list, num_chosen, data);

		/* Check for illegal combination */
		if (!callback_value)
		{
			/* Combination was illegal */
			return;
		}

		/* Check for ability to stop looking if desired */
		if (callback_value > 1 && checking_retreat) stop_choose = 1;

		/* Check for chooser's turn */
		if (chooser == g->turn)
		{
			/* Get number of legal combinations */
			num_legal = nodes[node_len].num_legal;

			/* Add combination */
			nodes[node_len].legal[num_legal] = chosen;

			/* One more combination */
			nodes[node_len].num_legal++;
		}
		else
		{
			/* Evaluate result */
			score = eval_game(&sim, chooser);

			/* Check for better score */
			if (score >= *b_s)
			{
				/* Save better */
				*b_s = score;
				*best = chosen;
			}
		}

		/* Done */
		return;
	}

	/* Try without current card */
	ai_choose_aux(g, chooser, who, choices, n - 1, c, chosen << 1,
	              best, b_s, callback, data);

	/* Try with current card (if more cards can be chosen) */
	if (c) ai_choose_aux(g, chooser, who, choices, n - 1, c - 1,
	                     (chosen << 1) + 1, best, b_s, callback, data);
}

/*
 * Generic card chooser.
 */
static void ai_choose(game *g, int chooser, int who, design **choices,
                      int num_choices, int min, int max, choose_result callback,
                      void *data, char *prompt)
{
	double b_s = -2;
	int best = 0;
	int c, i;
	design *chosen[DECK_SIZE];
	int num_chosen = 0;

	/* Check for unsimulated game */
	if ((!g->simulation && chooser == g->turn) || assist_str)
	{
		/* Check current action */
		if (best_path[best_path_pos].act == ACT_CHOOSE)
		{
			/* Get best from stored choice */
			best = best_path[best_path_pos].chosen;

			/* Loop over chosen cards */
			for (i = 0; (1 << i) <= best; i++)
			{
				/* Check for bit set */
				if (best & (1 << i))
				{
					/* Add card to list */
					chosen[num_chosen++] = choices[i];
				}
			}

			/* Advance to next path position */
			best_path_pos++;

			/* Just use previously computed choice */
			callback(g, who, chosen, num_chosen, data);

			/* Check for assist string */
			if (assist_str)
			{
				/* Add assist message */
				choose_assist(chosen, num_chosen);
			}

			/* Done */
			return;
		}
		else
		{
			/* Error */
			printf("No choice action next in path?!\n");
		}
	}

	/* Restrict maximum */
	if (max > num_choices) max = num_choices;

	/* Check for error */
	if (min > num_choices)
	{
		/* Error */
		printf("Can't choose %d from %d!\n", min, num_choices);
		return;
	}

	/* Check for error */
	if (!num_choices)
	{
		/* Error */
		fprintf(stderr, "No choices available ('%s')\n", prompt);
		return;
	}

	/* Prevent recursive calls to ai_choose() */
	if (inside_choose) return;
	
	/* We are inside choose function */
	inside_choose = 1;

	/* Check for chooser's turn */
	if (chooser == g->turn)
	{
		/* Set choice data */
		nodes[node_len].callback = callback;
		nodes[node_len].data = data;
		nodes[node_len].num_legal = 0;
		nodes[node_len].who = who;

		/* Set choices */
		for (i = 0; i < num_choices; i++)
		{
			/* Set choice */
			nodes[node_len].choices[i] = choices[i];
		}
	}

	/* Do not stop looking */
	stop_choose = 0;

	/* Loop over number of cards allowed */
	for (c = min; c <= max; c++)
	{
		/* Try choosing this many cards */
		ai_choose_aux(g, chooser, who, choices, num_choices, c, 0,
		              &best, &b_s, callback, data);
	}

	/* Check for chooser's turn */
	if (chooser == g->turn)
	{
		/* One more choice to make */
		node_len++;
	}
	else
	{
		/* Loop over chosen cards */
		for (i = 0; (1 << i) <= best; i++)
		{
			/* Check for bit set */
			if (best & (1 << i))
			{
				/* Add card to list */
				chosen[num_chosen++] = choices[i];
			}
		}

		/* Go ahead and perform choice */
		callback(g, who, chosen, num_chosen, data);
	}

	/* No longer inside choose function */
	inside_choose = 0;
}

/*
 * Decide whether to call bluff.
 */
static int ai_call_bluff(game *g)
{
	game sim;
	player *opp;
	card *c;
	double score;
	int i, unknown = 0, bluff = 0;

	/* Get bluffing player's pointer (it is still their turn) */
	opp = &g->p[g->turn];

	/* Loop over opponent cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &opp->deck[i];

		/* Check for correct bluff icon */
		if (c->d_ptr->icons & (ICON_BLUFF_F << g->fight_element))
		{
			/* Count cards with unknown locations */
			if (!c->loc_known) unknown++;
		}

		/* Count bluff cards */
		if (c->bluff) bluff++;
	}

	/* Always call impossible bluffs */
	if (bluff > unknown) return 1;

	/* Clear best path */
	best_path_pos = 0;

	/* Simulate game */
	simulate_game(&sim, g);

	/* XXX Force our turn */
	sim.turn = !sim.turn;
	sim.sim_turn = sim.turn;

	/* Start at beginning of turn */
	sim.p[sim.turn].phase = PHASE_START;

#ifdef DEBUG
	printf("NO CALL BLUFF START\n");
#endif

	/* Get score of not calling */
	score = find_action(&sim);

#ifdef DEBUG
	printf("NO CALL BLUFF END\n");
#endif

	/* Restart simulated game */
	simulate_game(&sim, g);

	/* XXX Force our turn */
	sim.turn = !sim.turn;
	sim.sim_turn = sim.turn;

	/* Start at beginning of turn */
	sim.p[sim.turn].phase = PHASE_START;

	/* Loop over opponent's cards */
	for (i = 1; i < DECK_SIZE; i++)
	{
		/* Get card pointer */
		c = &sim.p[!sim.turn].deck[i];

		/* Skip non-bluff cards */
		if (!c->bluff) continue;

		/* Discard bluff */
		reveal_bluff(&sim, !sim.turn, c->d_ptr);

		/* Mark card as fake */
		c->random_fake = 1;
	}

	/* Give opponent dragon */
	attract_dragon(&sim, !sim.turn);

#ifdef DEBUG
	printf("CALLED BLUFF START\n");
#endif

	/* Check for better options than before */
	if (find_action(&sim) >= score) return 1;

#ifdef DEBUG
	printf("CALLED BLUFF END\n");
#endif

	/* Do not call bluff */
	return 0;
}

/*
 * Perform final training and reset neural net.
 */
static void ai_game_over(game *g, int who)
{
	double result[2];

	/* Check for win */
	if (g->p[who].crystals)
	{
		/* Compute margin of victory */
		result[who] = .5 + g->p[who].crystals * 0.1;

		/* Check for instant win */
		if (g->p[who].instant_win) result[who] = 1.0;
	}
	else
	{
		/* Compute margin of defeat */
		result[who] = .5 - g->p[!who].crystals * 0.1;

		/* Check for instant loss */
		if (g->p[!who].instant_win) result[who] = 0.0;
	}

	/* Compute opponent's result */
	result[!who] = 1.0 - result[who];

	/* Perform final training */
	perform_training(g, who, result);

	/* Clear past input array */
	clear_store(&learner[who]);

	/* One more training iteration done */
	learner[who].num_training++;
}

/*
 * Shutdown AI and save neural net.
 */
static void ai_shutdown(game *g, int who)
{
	char fname[1024];

	/* Create network filename */
	sprintf(fname, DATADIR "/networks/bluemoon.net.%s.%s",
	                                     g->p[who].p_ptr->name,
	                                     g->p[!who].p_ptr->name);

	/* Save network weights to disk */
	save_net(&learner[who], fname);
}

/*
 * Set of AI functions.
 */
interface ai_func =
{
	ai_initialize,
	ai_take_action,
	ai_choose,
	ai_call_bluff,
	ai_game_over,
	ai_shutdown,
};
