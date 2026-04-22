## A simple preload library that measures code coverage

This library discovers shared libraries that get loaded into an executable at runtime.
It enables statistical profiling on those ELF objects that are deemed interesting.

This is still work in progress. It does something, but it is not ready for production
yet.

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

The default control file is `/etc/blanket.conf`. You can override this in two ways:

* by setting the `BLANKET_CONTROL` shell variable. This setting affects both the utility and the shared library.
* by using the `--control-path` command line switch of the utility. Note that this will affect _just_ the utility,
  but not the shared library.

### Displaying the current configuration

You can display your current settings using `blanket show`, like this:

```
# blanket show
Mode:                 timer
Sampling interval:    1000 nsec
Address granularity:  8
Address shift:        3
Measure all:          no

1 object entries
   dev          ino path
  0028       425390 /usr/bin/sha256sum
```

### Different sampling modes

There are different modes of measuring coverage. You can select the mode using the `--mode` option, like this:

```
# blanket update --mode touch
```

These are the modes currently supported:

* `touch`: This is probably the simplest mode. It just records when an ELF object was used, and by which application.
* `timer`: In this mode, an interval timer is set up that samples the instruction pointer at regular
  intervals, and records the address. This can give you an approximate idea of what areas of the code
  are covered. However, as it is timer based, this is nowhere near complete, and will probably only produce
  realistic results in code that runs for a long time, or executes certain loops *a lot*.
* `mcount`: This mode expects the ELF binaries you want to inspect to be compiled with profiling
  support (`-pg`). The library will just intercept every call to `mcount()` and record the caller's
  instruction pointer. This gives a reasonable picture of the functions that are called, but not
  of the code paths within the functions.
* `ptrace`: In this mode, you can request specific functions to be analyzed by using the `ptrace()`
  system call to single-step through the function when it is invoked. You need to use `blanket trace`
  rather than `blanket add` to specify the ELF objects and functions you want to measure.

### Notes on what gets covered

Note that coverage data is generated and written for mapped ELF objects. In particular, if you enable
sampling for an ELF binary, blanket will sample and write coverage data for this ELF binary, but not
automatically for all shared libraries that it loads.

### Reporting

Once you have run your test case(s), you can evaluate the measured coverage, like this:

```
# blanket report /tmp/coverage-*
/usr/bin/sha256sum
Text:             00000298-00007c96
Test ID:              0
Sampling size:       16
Global coverage: 26.92%
    7.4% sha256_process_block
    5.3% sha256_conclude_ctx
   25.0% sha224_finish_ctx
   20.0% sha256_process_bytes
   19.0% sha256_buffer
   19.0% sha224_buffer
   20.0% shaxxx_stream.isra.1
   50.0% sha256_stream
   50.0% sha224_stream
   ...

```

The above is the result of using `sha256sum` to compute the hash over several large files. As you can
see, the test covered about 27% of all code in the binary's `.text` section. Breakdown by function shows
how much of each function we reached. Note that these numbers provide a lower bound estimate only, as
the sampling in this example is only.


### Hooks

The library tries to hook into the executing process in several ways

* via a constructor callback that gets invokved prior to entering `main()`
* by providing an `mcount()` hook to intercept applications compiled with profiling
* by providing a `dlopen()` hook to capture on-demand loading of shared libraries at runtime
* by providing a `pthread_create()` hook to capture an application creating new threads.
  This is needed to enable timer based sampling also for threads.

Caveat: using `ptrace` mode with multithreaded applications is probably not going to work very well.
This area needs further work.

### Source code annotation

If you have an application compiled with debug symbols, and you have the source code around,
it's possible to display which parts of a function were reached. As this requires precise
sampling, you need to sample with a granularity of 1. It does work with `timer` mode, but for the
most precise results, you want to use this with `ptrace` mode.

Here's a simple example how to use it:

```
# blanket init --mode ptrace
# blanket trace ./tests/dlopen main
# LD_PRELOAD=$PWD/libblanket.so ./tests/dlopen
# blanket report --annotate /tmp/coverage-*
/home/okir/wip/sillycov/tests/dlopen
Mode:             3
ELF text section: 00000610-00000890
Sampling size:    1
Global coverage:  58 hits
Source files and their coverage:
/home/okir/wip/sillycov/tests/dlopen.c
--- line 9: ---

   static double
   elapsed(void)
 + {
        static double t0 = 0;

--- line 19: ---

   int
   main(void)
 + {
        void *h;
        double (*sqrt)(double);
        unsigned int i, j;
        double accum;

 +      h = dlopen("/lib64/libm.so.6", RTLD_LAZY);
 +      if (h == NULL) {
                printf("Could not /lib64/libm.so.6: %s\n", dlerror());
                return 1;
        }

 +      sqrt = dlsym(h, "sqrt");
 +      if (sqrt == NULL) {
                printf("Could not find sqrt: %s\n", dlerror());
                return 1;
        }

 +      for (i = 0, accum = 0; elapsed() < 2; ) {
 +              for (j = 0; j < 1000; ++i, ++j)
 +                      accum += sqrt((double) i);
        }

 +      printf("Performed %u sqrt calls\n", i);
 +      return (accum == 0);
 + }

```

### TODO

We definitely should be able to use ptrace mode with an entire ELF object, not just with individual
functions.

### Environment

You can enable some debug output by setting BLANKET_TRACE=1.
