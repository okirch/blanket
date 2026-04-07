## A simple preload library that measures code coverage

This library discovers shared libraries that get loaded into an executable at runtime.
It enables statistical profiling on those ELF objects that are deemed interesting.

This is still work in progress. It does something, but it is not ready for production
yet.

To sort of show-case how it is supposed to work:

```
LD_PRELOAD=./libblanket.so BLANKET_TRACE=1 BLANKET_MEASURE_ALL=1 ./tdl
```


### How it works

There is a shared library called `libblanket.so`, which is preloaded into applications. You can either
do this by using the `LD_PRELOAD` shell variable, or by adding it to `/etc/ld.so.preload`.

By default, the library does not do much. It will check for a control file to find out what it is supposed
to do; if that file does not exist, nothing will happen.

The default location of the control file is `/etc/blanket.conf`. It is created and modified using the
`blanket` utility. Here's a typical sequence of commands:

```
# blanket init
# blanket add /usr/lib64/libcrypto.so.*
```

This creates the control file with some default settings, and marks `libcrypto.so.*` for tracing.
The next time anyone executes a command that uses libcrypto, blanket will detect that we're interested in
coverage for this library, and enable coverage sampling for it. The resulting coverage data is written
to a file (currently, `/tmp/coverage-*`).

Note that coverage data is generated and written for mapped ELF objects. In particular, if you enable
sampling for an ELF binary, blanket will sample and write coverage data for this ELF binary, but not
automatically for all shared libraries that it loads.

Once you have run your test case(s), you can evaluate the measured coverage, like this:

```
# blanket show /usr/bin/sha256sum
/usr/bin/sha256sum
Text:             00000298-00007c96
Test ID:              0
Sampling size:       16
Global coverage: 26.92%
   78.6% sha256_process_block
   23.5% shaxxx_stream.isra.1
```

The above is the result of using `sha256sum` to compute the hash over several large files. As you can
see, the test covered about 27% of all code in the binary's `.text` section. Breakdown by function shows
that almost 80% of all code in `sha256_process_block()` was exercised by our test case, and a quarter of
a helper function being exercised.


### TODO

Right now, reporting will analyze the coverage of a single process being run (as each run produces a
separate output file). When looking at test cases that involve several command invocations, it does not
help that each time, we exercised maybe 25% of some function - we want to know if, across all our test
cases, we exercise all code paths, or whether we always exercise the same 25% of that function.

In other words, we need a way to aggregate coverage from different runs before we report the output.

Other useful things that may be worth trying:

* coverage per source file
* coverage per source line (needs dwarf handling)

### Environment

You can enable some debug output by setting BLANKET_TRACE=1.
