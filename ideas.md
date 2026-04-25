 
Hi. sharing idea.

make a scripting language with very simple jit that is always unoptimized
so jit is just fastpath asm macros that are copy pasted + most of complex code is in c

the trick to make it very fast i never seen: 
allocator based pointer tagging.

use custom mmap malloc to allocate

000RRRRRR TTTTTT XXXXX000

r - random
TT - tag bits
xxx - object in arena
000 align

by controlling allocator can fit plenty of tag bits to pointer to tag object types and check them with one bitop without touching object header!
pipeline of cpu will hide this, no need to prove types guards are super fast

pointers are valid pointers, no need to unmask ungtag!


This project is: lets start a language from object system, allocator, gc.

Only then we will proceed to interpretter and vm.
This will also simplify writing compiler itself


type system i had in my prototype language

object types, binary
for programming lang vm

pointers have align at least 8 so last 3 bits 000

here bitcount isnt as real longer words, just simplified scheme

PPPPPP000

VVVVVVVV1 int packed, unpacked as >> 1, V value bits

000000000 - null = undef
000000010 - bool false
111111110 - bool true

what want to add

PPTTTTPPP000

TTTT - tag

interface should be something like

type - tag value, 8 bits would be enough. 6 fine

#define O_MAP 0x04
#define O_ARRAY 0x08


tg_register_type(O_MAP, 128)
tg_register_type(O_ARRAY, 32)

void* result = tg_alloc(O_MAP)

objects will be 32-512 byte sized, maybe up to 1-2kb, not reallocable

i need tags only for object headers, the complex dynamic objects will have generic resizeable areas inside.

PTR_TAG() extract TTT from ptr





