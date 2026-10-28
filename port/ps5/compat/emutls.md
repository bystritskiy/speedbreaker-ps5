# Emulated TLS lifetime on PS5

`emutls.c` is the standalone LLVM implementation from
`mihawk-99/PS5_PayloadSDK`, commit
`b83202be73e930050e00de0f1b4d9ec46c0391bf`, `libc/emutls.c`.
It retains its Apache-2.0 WITH LLVM-exception license header.

The port changes the delayed-cleanup condition to include `__PROSPERO__`.
PS5's platform implementation of `__cxa_thread_atexit_impl` runs C++ TLS
destructors from a pthread key. The normal non-Bionic emutls implementation
frees the TLS storage on the first key-cleanup round, before that callback
uses it. Without the change, the first guest thread to exit after the
opening frames crashes in `mspace_free`, called by a destructor with a freed
object.

As on Bionic, keep the emutls array registered for one additional cleanup
round. C++ destructors run while their objects are still alive, and the
next round frees the storage. This is linked as an explicit object before
the builtins archive, so its default zero-delay implementation is not used.

`tests/ps5_emutls_test.c` checks that pthread-key cleanup can use the TLS
object, and that its storage is freed afterwards; with the original
zero-delay implementation it fails.

From the repository root, using the host's Clang and AddressSanitizer:

```sh
clang -std=c11 -O1 -g -fsanitize=address -pthread -D__PROSPERO__=1 \
  -DEMUTLS_USE_POSIX_MEMALIGN=1 -Dfree=ps5_test_free \
  -c port/ps5/compat/emutls.c -o build/ps5-emutls-test.o
clang -std=c11 -O1 -g -fsanitize=address -pthread \
  tests/ps5_emutls_test.c build/ps5-emutls-test.o -o build/ps5-emutls-test
build/ps5-emutls-test
```
