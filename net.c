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

#include "net.h"

/* #define NOISY */

/*
 * Maximum number of previous input sets.
 */
#define PAST_MAX 50

/*
 * Create a random weight value.
 */
static double random_weight(void)
{
	/* Return a random value */
	return 0.2 * rand() / RAND_MAX - 0.1;
}

/*
 * Create a network of the given size.
 */
void make_learner(net *learn, int input, int hidden, int output)
{
	int i, j;

	/* Set number of outputs */
	learn->num_output = output;

	/* Set number of inputs */
	learn->num_inputs = input;

	/* Number of hidden nodes */
	learn->num_hidden = hidden;

	/* Create input array */
	learn->input_value = (int *)malloc(sizeof(int) * (input + 1));

	/* Create array for previous inputs */
	learn->prev_input = (int *)malloc(sizeof(int) * (input + 1));

	/* Create hidden sum array */
	learn->hidden_sum = (double *)malloc(sizeof(double) * hidden);

	/* Create hidden result array */
	learn->hidden_result = (double *)malloc(sizeof(double) * (hidden + 1));

	/* Create hidden error array */
	learn->hidden_error = (double *)malloc(sizeof(double) * hidden);

	/* Create output result array */
	learn->net_result = (double *)malloc(sizeof(double) * output);

	/* Create output probability array */
	learn->win_prob = (double *)malloc(sizeof(double) * output);

	/* Last input and hidden result are always 1 (for bias) */
	learn->input_value[input] = 1;
	learn->hidden_result[hidden] = 1.0;

	/* Create rows of hidden weights */
	learn->hidden_weight = (double **)malloc(sizeof(double *) *
	                                         (input + 1));

	/* Loop over hidden weight rows */
	for (i = 0; i < input + 1; i++)
	{
		/* Create weight row */
		learn->hidden_weight[i] = (double *)malloc(sizeof(double) *
		                                           hidden);

		/* Randomize weights */
		for (j = 0; j < hidden; j++)
		{
			/* Randomize this weight */
			learn->hidden_weight[i][j] = random_weight();
		}
	}

	/* Create rows of output weights */
	learn->output_weight = (double **)malloc(sizeof(double *) *
	                                         (hidden + 1));

	/* Loop over output weight rows */
	for (i = 0; i < hidden + 1; i++)
	{
		/* Create weight row */
		learn->output_weight[i] = (double *)malloc(sizeof(double) *
		                                           output);

		/* Randomize weights */
		for (j = 0; j < output; j++)
		{
			/* Randomize this weight */
			learn->output_weight[i][j] = random_weight();
		}
	}

	/* Clear hidden sums */
	memset(learn->hidden_sum, 0, sizeof(double) * hidden);

	/* Clear hidden errors */
	memset(learn->hidden_error, 0, sizeof(double) * hidden);

	/* Clear previous inputs */
	memset(learn->prev_input, 0, sizeof(int) * (input + 1));

	/* Create set of previous inputs */
	learn->past_input = (int **)malloc(sizeof(int *) * PAST_MAX);

	/* No past inputs available */
	learn->num_past = 0;

	/* No training done */
	learn->num_training = 0;
}

/*
 * Normalize a number using a 'sigmoid' function.
 */
static double sigmoid(double x)
{
	/* Return sigmoid result */
	return 1.0 / (1.0 + exp(-x));
}

/*
 * Compute a neural net's result.
 */
void compute_net(net *learn)
{
	int i, j;
	double sum;

	/* Loop over inputs */
	for (i = 0; i < learn->num_inputs + 1; i++)
	{
		/* Check for difference from previous input */
		if (learn->input_value[i] != learn->prev_input[i])
		{
			/* Loop over hidden weights */
			for (j = 0; j < learn->num_hidden; j++)
			{
				/* Adjust sum */
				learn->hidden_sum[j] +=
				                learn->hidden_weight[i][j] *
				                       (learn->input_value[i] -
				                        learn->prev_input[i]);
			}

			/* Store input */
			learn->prev_input[i] = learn->input_value[i];
		}
	}
	
	/* Normalize hidden node results */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Set normalized result */
		learn->hidden_result[i] = sigmoid(learn->hidden_sum[i]);
	}

	/* Clear probability sum */
	learn->prob_sum = 0.0;

	/* Then compute output nodes */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Start sum at zero */
		sum = 0.0;

		/* Loop over hidden results */
		for (j = 0; j < learn->num_hidden + 1; j++)
		{
			/* Add weighted result to sum */
			sum += learn->hidden_result[j] *
			       learn->output_weight[j][i];
		}

		/* Save sum */
		learn->net_result[i] = sum;

		/* Track total output */
		learn->prob_sum += exp(sum);
	}

	/* Then compute output probabilities */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Compute probability */
		learn->win_prob[i] = exp(learn->net_result[i]) /
		                     learn->prob_sum;
	}
}

/*
 * Store the current inputs into the past set array.
 */
void store_net(net *learn)
{
	int i;

	/* Check for too many past inputs already */
	if (learn->num_past == PAST_MAX)
	{
		/* Destroy oldest set */
		free(learn->past_input[0]);

		/* Move all inputs up one spot */
		for (i = 0; i < PAST_MAX - 1; i++)
		{
			/* Move one set of inputs */
			learn->past_input[i] = learn->past_input[i + 1];
		}

		/* We now have one fewer set */
		learn->num_past--;
	}

	/* Make space for new inputs */
	learn->past_input[learn->num_past] = malloc(sizeof(int) *
	                                            (learn->num_inputs + 1));

	/* Copy inputs */
	memcpy(learn->past_input[learn->num_past], learn->input_value,
	       sizeof(int) * (learn->num_inputs + 1));

	/* One additional set */
	learn->num_past++;
}

/*
 * Clean up past stored inputs.
 */
void clear_store(net *learn)
{
	int i;

	/* Loop over previous stored inputs */
	for (i = 0; i < learn->num_past; i++)
	{
		/* Free inputs */
		free(learn->past_input[i]);
	}

	/* Clear number of past inputs */
	learn->num_past = 0;
}

/*
 * Train a network so that the current results are more like the desired.
 */
void train_net(net *learn, double lambda, double *desired)
{
	int i, j, k;
	double error, corr, deriv, hderiv;
	double *hidden_corr;
#ifdef NOISY
	double orig[5];
#endif

#ifdef NOISY
	for (i = 0; i < learn->num_output; i++)
	{
		orig[i] = learn->win_prob[i];
	}
#endif

	/* Loop over output nodes */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Compute error */
		error = lambda * (learn->win_prob[i] - desired[i]);

		/* Output portion of partial derivatives */
		deriv = learn->win_prob[i] * (1.0 - learn->win_prob[i]);

		/* Loop over node's weights */
		for (j = 0; j < learn->num_hidden; j++)
		{
			/* Compute correction */
			corr = -error * learn->hidden_result[j] * deriv;

			/* Compute hidden node's effect on output */
			hderiv = deriv * learn->output_weight[j][i];

			/* Loop over other output nodes */
			for (k = 0; k < learn->num_output; k++)
			{
				/* Skip this output node */
				if (i == k) continue;

				/* Subtract this node's factor */
				hderiv -= learn->output_weight[j][k] *
				          exp(learn->net_result[i] +
				              learn->net_result[k]) /
				          (learn->prob_sum * learn->prob_sum);
			}

			/* Compute hidden node's error */
			learn->hidden_error[j] += error * hderiv;

			/* Apply correction */
			learn->output_weight[j][i] += learn->alpha * corr;
		}

		/* Compute bias weight's correction */
		learn->output_weight[j][i] += learn->alpha * -error * deriv;
	}

	/* Create array of hidden weight correction factors */
	hidden_corr = (double *)malloc(sizeof(double) * learn->num_hidden);

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Output portion of partial derivatives */
		deriv = learn->hidden_result[i] *
			(1.0 - learn->hidden_result[i]);

		/* Calculate correction factor */
		hidden_corr[i] = deriv * -learn->hidden_error[i] * learn->alpha;
	}

	/* Loop over inputs */
	for (i = 0; i < learn->num_inputs + 1; i++)
	{
		/* Skip zero inputs */
		if (!learn->input_value[i]) continue;

		/* Loop over hidden nodes */
		for (j = 0; j < learn->num_hidden; j++)
		{
			/* Adjust weight */
			learn->hidden_weight[i][j] += hidden_corr[j];
		}
	}

	/* Destroy hidden correction factor array */
	free(hidden_corr);

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Clear node's error */
		learn->hidden_error[i] = 0;

		/* Clear node's stored sum */
		learn->hidden_sum[i] = 0;
	}

	/* Clear previous inputs */
	memset(learn->prev_input, 0, sizeof(int) * (learn->num_inputs + 1));

#ifdef NOISY
	compute_net();
	for (i = 0; i < learn->num_output; i++)
	{
		printf("%lf -> %lf: %lf\n", orig[i], desired[i], learn->win_prob[i]);
	}
#endif
}

/*
 * Load network weights from disk.
 */
int load_net(net *learn, char *fname)
{
	FILE *fff;
	int i, j;
	int input, hidden, output;

	/* Open weights file */
	fff = fopen(fname, "r");

	/* Check for failure */
	if (!fff) return -1;

	/* Read network size from file */
	fscanf(fff, "%d %d %d\n", &input, &hidden, &output);

	/* Check for mismatch */
	if (input != learn->num_inputs ||
	    hidden != learn->num_hidden ||
	    output != learn->num_output) return -1;

	/* Read number of training iterations */
	fscanf(fff, "%d\n", &learn->num_training);

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Loop over weights */
		for (j = 0; j < learn->num_inputs + 1; j++)
		{
			/* Load a weight */
			if (fscanf(fff, "%lf\n",
			           &learn->hidden_weight[j][i]) != 1)
			{
				/* Failure */
				return -1;
			}
		}
	}

	/* Loop over output nodes */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Loop over weights */
		for (j = 0; j < learn->num_hidden + 1; j++)
		{
			/* Load a weight */
			if (fscanf(fff, "%lf\n",
			           &learn->output_weight[j][i]) != 1)
			{
				/* Failure */
				return -1;
			}
		}
	}

	/* Done */
	fclose(fff);

	/* Success */
	return 0;
}

/*
 * Save network weights to disk.
 */
void save_net(net *learn, char *fname)
{
	FILE *fff;
	int i, j;

	/* Open output file */
	fff = fopen(fname, "w");

	/* Save network size */
	fprintf(fff, "%d %d %d\n", learn->num_inputs, learn->num_hidden,
	                           learn->num_output);

	/* Save training iterations */
	fprintf(fff, "%d\n", learn->num_training);

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Loop over weights */
		for (j = 0; j < learn->num_inputs + 1; j++)
		{
			/* Save a weight */
			fprintf(fff, "%.12le\n", learn->hidden_weight[j][i]);
		}
	}

	/* Loop over output nodes */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Loop over weights */
		for (j = 0; j < learn->num_hidden + 1; j++)
		{
			/* Save a weight */
			fprintf(fff, "%.12le\n", learn->output_weight[j][i]);
		}
	}

	/* Done */
	fclose(fff);
}
