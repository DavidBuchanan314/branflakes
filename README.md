# branflakes
Yet another x86_64 optimising Brainf*ck JIT compiler. It's very fast, and buggy.

# Issues:
- Doesn't work properly with some programs (notably hanoi.b).
- Insufficient bounds checks - a malicious program could overwrite abritrary memory locations.
- The code is poorly written (I don't really know C *or* x86 assembly)

# TODO:
-  More optimisations.
-  The `,` instruction.
