# snd-egd
Copyright 2008-2022 Nicholas J. Kain
See LICENSE for licensing information.

## Introduction

snd-egd uses a standard audio input device (such as line-in or mic) to
produce a stream of random numbers that it feeds to the OS random device
(`/dev/random`, `/dev/urandom`, and `getrandom()`).

Effectively it allows any normal PC audio device to act as a high-quality
hardware random number generator by measuring thermal noise.

## Requirements

* Linux kernel (with ALSA)
* GCC or Clang
* GNU Make

## Installation

Edit defines.h as necessary.

Compile and install snd-egd.
* Build snd-egd: `make`
* Install the `snd-egd/snd-egd` executable in a normal place.  I would
  suggest `/usr/sbin` or `/usr/local/sbin`.

Create a new user and group for snd-egd, and create an empty directory for
its chroot jail. (_optional_)

Set your sound card mixer settings so that a non-muted recording channel
is available.  Confirm that it is producing output.  Turn up the gain
as high as is possible without compressing the dynamic range of the output.

Run snd-egd as a root user with a command line similar to the following:

`snd-egd -nv -u snd-egd -c /var/empty`

Drain the kernel random device of stored entropy by running the
following command in another terminal:

`cat /dev/urandom >| /dev/null`

snd-egd should print to its controlling terminal output indicating that
the random device is being filled back with sample entropy as it is being
drained.

Check the frequency distribution of the generated entropy by either
exiting snd-egd (`ctrl+c` or send a signal) or by sending it the `SIGUSR1`
signal.  Make sure that the generated output looks fairly uniform in
dispersion.  Note that a fair number of samples should be taken to
judge uniformity; truly random data will contain nonuniformity in small
sample sets.

If everything looks good, run on a permanent basis by using a command
similar to the following:

`snd-egd -nv -u snd-egd -c /var/empty`

Add the command to your init scripts if you wish for snd-egd to run
at startup.

## Theory of Operation

Thermal noise is real randomness, but it might not be well-distributed, so
the trick is to quickly normalize it without degrading the random quality.
A simple way to do that is to just take edges: for any two bits, 00 ->
skip, 11 -> skip, 01 -> 0, 10 -> 1.  This method normalizes the output
of any unfair 'coin' so long as the probability of each outcome is fixed
for any given 'flip'.  It's sometimes known as von Neumann's method.

It is also possible to extract more randomness from a normalized sequence
by applying the above method recursively: 00/11 pairs and 01/10 pairs
are each treated as new sequences.  This method is sometimes called the
Advanced Multi-level Strategy (AMLS) in the literature.

AMLS is used in snd-egd, but only to a single depth of iteration.
Since every depth of iteration can only generate half as many bits as
the previous depth and requires twice as much temporary storage, and
AMLS performance is dependent on very long inputs to perform at its
theoretical level, deeply recursive AMLS is not useful.

However, a sound card's signal isn't really random when represented as
raw samples.  If the input signal is a random walk for any given bit,
then what really changes unpredictably for any given sample is the
difference in value (drift).  In the actual program, the absolute value
of the difference is used.  The reason is that for any true random walk,
over a finite length of that walk, it will always exhibit a bias in
direction that increases with time.

The described algorithm gives nice properties.  Better dynamic range
yields faster random generation, but if the full input range is unused,
or if compression/clipping is occuring, the algorithm discards the
unrandom bits transparently.

## Implementation Details

The level of random bits present in the kernel random device (KRD) is
monitored.  When a watermark is passed, snd-egd wakes up.  If snd-egd's
internal ring buffer of samples is still populated, stored entropy
is immediately put into the KRD and the ring buffer is then refilled.
If the ring buffer is not full, then the ring buffer is refilled before
entropy is stored as described before.

The ring buffer used is not a traditional ring buffer.  The only
distinction made is whether a byte in the ring buffer is filled with new
entropy.  New entropy is xored with the old entropy when it is added.
The position in the buffer does not vary as a 'ring' in any given
iteration -- the path through the buffer wanders unpredictably.

Input is sampled from the sound card using the method described above in
the 'Theory of Operation' section.  Both the left and right channels are
used in 48000Hz 16-bit mode.  Each bit in each channel is treated as a
separate bitstream.  When a full byte of input from any given bitstream
is gathered, it is added to the ring buffer of stored entropy.

All memory areas containing entropy are locked into RAM so that they
cannot be swapped to disk.  Careful attention is paid to maximize
performance -- dynamic memory allocations are not used in any of the main
paths, and the inner loops should easily fit into even small processor
caches.  Support exists for use of POSIX capabilities to allow the daemon to
run as a user with only `CAP_SYS_ADMIN`, and snd-egd can restrict itself to a
chroot.

## Downloads

* [GitLab](https://gitlab.com/niklata/snd-egd)
* [Codeberg](https://codeberg.org/niklata/snd-egd)
* [BitBucket](https://bitbucket.com/niklata/snd-egd)
* [GitHub](https://github.com/niklata/snd-egd)

## Possible Improvements

* Support pipewire
* Automatically adjust gain of input source to maximize dynamic range.

