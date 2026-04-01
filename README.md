## A simple preload library that measures code coverage

This library discovers shared libraries that get loaded into an executable at runtime.
It enables statistical profiling on those ELF objects that are deemed interesting.

This is still work in progress. It will not do much.

To sort of show-case how it is supposed to work:

```
LD_PRELOAD=./libblanket.so BLANKET_TRACE=1 BLANKET_MEASURE_ALL=1 ./tdl
```


### Environment

You can enable some debug output by setting BLANKET_TRACE=1.
