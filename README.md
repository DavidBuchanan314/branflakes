# branflakes
Yet another x86_64 optimising Brainf*ck JIT compiler. It's very fast, and buggy.

### Performance:
Performance is significantly better than a dumb interpreter, but still far from the best JITs.

|          | [Beef](http://kiyuko.org/software/beef) | [copy.sh](https://copy.sh/brainfuck/) | Branflakes | [Tritium](https://github.com/rdebath/Brainfuck) |
|----------|-----------------------------------------|---------------------------------------|------------|-------------------------------------------------|
| mandel.b | 4m36.388s                               | 0m3.99s                               | 0m2.279s   | 0m1.084s                                        |

### Issues:
- Doesn't work properly with some programs (notably hanoi.b).
- Insufficient bounds checks - a malicious program could overwrite abritrary memory locations.
- The code is poorly written (I don't really know C *or* x86 assembly)

### TODO:
-  More optimisations.
-  The `,` instruction.
