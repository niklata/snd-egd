/* AMLS version 0.5

   Convert a biased uncorrelated bitstream into an unbiased
   uncorrelated bitstream.

   http://www.ciphergoth.org/software/unbiasing

   Paul Crowley <paul@ciphergoth.org>, corners@sbcglobal.net
   December 2001

   You can do anything you want with it. It comes with a "garbage
   man's guarantee". Satisfaction guaranteed, or double your garbage
   back.  (In other words, this code has NO WARRANTY, express or
   implied, as with points 11 and 12 of the GPL.)

   This source is based on an algorithm called "Advanced Multilevel
   Strategy" described in a paper titled "Tossing a Biased Coin" by
   Michael Mitzenmacher

   http://www.fas.harvard.edu/~libcs124/CS/coinflip3.pdf

   To facilitate storage this recursive implementation does not
   process the bits in the same order as described in the paper.  It
   does not produce exactly the same output, but the output will be
   unbiased if the input is truly uncorrelated.  */

/* Each input byte must be '0' or '1' (ASCII).  Other inputs will
   cause invalid output.

   The output is ASCII zeroes and ones. The length of the output is
   strictly less than that of the input. */

static void amls_round(
    char *input_start,
    char *input_end,
    char **output /* Moving output pointer */
                )
{
    char *low = input_start;
    char *high = input_end -1;
    char *doubles = input_start;

    if (high <= low)
        return;
    do {
        if (*low == *high) {
            *doubles++ = *low;
            *high = '0';
        } else {
            *(*output)++ = *low;
            *high = '1';
        }
        low++;
        high--;
    } while (high > low);
    amls_round(input_start, doubles, output);
    amls_round(high + 1, input_end, output);
}
