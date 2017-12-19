AppNexus Common Framework (ACF)
===============================

This C library contains core components on which other higher level
libraries and applications can build.

We have packaged some of our "common" code used by applications in the
real-time platform; that is the original basis for ACF.  However,
that's only a small portion of the fundamental code we use in
production, and it's not clear that the current packaging approach is
the best way for us to reuse code while allowing users to decide how
much they would like to opt in our internal framework.

We thus decided to publish most of the remaining shared code under
`common/`, despite the lack of packaging.  Suboptimal packaging should
not stand in the way of progress, and it should be easy for others to
copy from `common/` and remove AppNexus-specific noise.

Our goal with this initial code dump is to share read-optimised data
structures implemented in C.  The implementation is usually not all
we dreamed of, and the dependency graph is sometimes unreasonable.
However, the code has proved its mettle in production, builds to our
real needs, and is known to not be overly bug-prone for users.  We
don't doubt that others have similar solutions to similar problems,
and we hope that we can all learn from each other, even if we can't
always immediately use one another's code.

Eventually, all that should be nicely packaged in the ACF library.  In
the meantime, we'd rather share as much as we easily can, even if the
source has unimportant dependencies on unpublished, more specialised,
support code.

We also have some other code that requires more buy-in, but that we
either feel can be useful to others, or must be present to make sense
of the more generic data structure and algorithm code.

The files in `src/` and `include/` is the small subset that we mostly
extricated from our internal ~never~ organically designed framework.
Other files that we feel are worthwhile to share in their current
state are in `common/`.  One day, `common/` will be empty and its
contents in `src/` and `include/`.

The contents of `common/` fall under three classes:

1. Read/space -optimised data structures or non-blocking algorithms in C;
2. More generic support code that build on some of what we've learned
   about running a mixture of CPU- and network- intensive HTTP servers.
3. Code that we may not be proud of, but is necessary to make sense
   of the former two classes.

We also have some smoke test code in `common/check`.

### Data structures and non-blocking algorithms

* `an_array`: more utility functions over the `an_array` already in
  acf.
* `an_average`: racy exponential moving average.
* `an_dict`, `an_dict_common`: type-safe, less bug-prone, wrappers for
  ConcurrencyKit's `ck_hs` SPMC hash set.
* `an_hook`: fast dynamic feature flags via self-modifying
  (cross-modifying) code.
* `an_hrlock`: hashed "big reader" reader-writer lock.
* `an_hrw`: rendezvous hashing to pick elements from `an_dict` sets.
* `an_interpolation_table`: a practical implementation of Demaine's,
  Jones's, and Patrascu's static interpolation search (Interpolation
  Search for Non-Independent Data, SODA 2004)
* `an_interval`: a practical implementation of Chazelle's filtering
  search for stabbing queries over machine integers (Filtering search:
  a new approach to query answering, SIAM Journal on Computing 1986)
* `an_itoa`: a faster integer to decimal string conversion.  It seems
  everyone makes the mistake of initially using human-readable strings
  for systems that eventually become bottlenecked on `itoa`.  This
  file was our stopgap while transitioning to a binary format.  Yes,
  it is reliably faster than the branchy Facebook code.
* `an_map`: another type-safe, easier to use, wrapper for
  ConcurrencyKit, this time `ck_ht`, the SPMC hash map.
* `an_qsort.inc`: just some trivial changes to the FreeBSD quicksort
  to specialise it at compile-time. `AN_QSORT_ENABLE_INSERTION_SORT_HEURISTIC`
  is useful to turn off an optimisation that sometimes goes quadratic.
* `an_rand`: convenient wrapper around xorshift128+.
* `an_ring`: two-phase enqueue/dequeue for ConcurrencyKit's `ck_ring`.
  Useful for ring buffers of large, partially used, items.
* `an_sampling`: windowed uniform sampling.
* `an_sparse_bitmap`: stabbing queries for disjoint intervals of
  machine integers.
* `an_sstm`: single-writer multi-version object-level transactional memory.
* `an_streaming_quantile`: concurrent streaming quantile estimation.
* `an_swlock`: compact single-writer multi-reader lock.
* `an_zorder`: Morton/Z-ordering utilities.
* `btree`: sorted flat container.
* `int_set`: specialised btree for machine integers.
* `log_linear_bin`: jemalloc-style log-linear binning, or,
  equivalently, high dynamic range histogram buckets.
* `tsearch.inc`: unrolled ternary search for sorted arrays.

### Generic support code

* `an_buf`: It's a constant battle to keep the number of buffer types
  under control, especially as we depend on new libraries.  `an_buf`
  is our attempt at a compatibility layer while we actually delete
  and merge buffer types.
* `an_cc`: Compile-time trickery.
* `an_malloc`: Multi-threaded tracked allocations, *and* a bump pointer
  allocator for overlapping transaction lifetimes, in one huge file.
* `an_md`, `x86_64`: Hardware timestamp counter code.
* `an_poison`: That's our list of functions we definitely do not want
  to use.
* `an_server`: Asynchronous HTTP server with some congestion control.
* `an_smr`: A wrapper for safe-memory reclamation that we find easier
  to use for the AppNexus context, where it's more important that
  data update code be easy to maintain than for it to be fast.
* `an_syslog`: Our old syslog sometimes deadlocks.  Some may enjoy
  this hack to centralise syslog production and to throttle identical
  messages.
* `an_table_hash`: We used to have a lot of different hash functions.
  We now only use murmurhash.
* `memory`: Some memory management primitives.
* `rtbr`: SMR driven by real time (i.e., the timestamp counter) instead
  of quiescence points or discrete epochs.

### Necessary evil code

* `an_string`: Clones of standard string manipulation functions that
  track allocations in `an_malloc`.
* `an_thread`: We ported many concepts directly from OS code, with
  one worker per core.  Doing so means we need per-thread blocks
  (to replace per-cpu storage), and these blocks of data must be
  safe to read from other threads.  `an_thread` is our monolithic
  per-thread storage block.  If we were to do it again, we'd probably
  avoid baking in the assumption of a pre-allocated set of per-thread
  data blocks; even the bounded (at most 32) set of workers is becoming
  an issue.
* `an_time`: Various time manipulation utility functions.
* `net/protocol/http-parser`: Vendored code for Joyent's HTTP parser.
* `util`: Grab bag of who-knows-what.  Despite everyone's best
  intentions, this sort of file seems to always happen.

How to build
------------
### Locally:
* `mkdir build`
* `cd build`
* `cmake ..`
* `make -j`

If you want to install locally, you can do
* `sudo make install`
However we don't recommend doing this since there is no
`make uninstall` target.

Unit tests
------------
After build, you can run following command in build directory to run unit tests
* `make test`
