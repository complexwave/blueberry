
Cma - C match, also polish Moth

this is lpeg-like library for C
with heavy syntax nectar (as Moths like)


S("string") - match string
R("abcdef") - any character in set/range, R("A-Za-z1234") "-" indicates range

[seq] - accepts many args a,b,c ...

OR, O([seq]) - ordered choice, tries patterns in sequence
AND, A([seq]) - matches all patterns in sequence or none

NOT([seq]) - matches if A([seq]) doesnt match

// lpeg patt1 - patt2  	Matches patt1 if patt2 does not match
A(NOT(patt2), patt1)

AHEAD([seq]) - match A([seq]) bug consume no input
BEFORE([seq]) - look before ( not sure yet how to implement it, will look how lpeg does it has limits - wnats pattern to be fixed length

REP(from, to, [seq]) - matches
MIN(N, [seq]) - REP(N, MATCH_INF, [seq])
MAX(N, [seq]) - REP(0, N, [seq])

CAP([seq]) -- capture object
CAPn([seq], "name") 

CAPG([seq]) capture group,
CAPF([seq], callback) -- call function


typedef {
	code (cma_state*) fnptr
	u16 type
	u16 flags
	// moredata - opcode specific structs inherit
} cma_op


{
	
	type = CMA_STR
	flags = CMA_OWN // opcode owns copy of buffer and frees it, or no if not set
	
	char * buf
	char *len
} cma_op_str;


{
	type = CMA_SET
	
	bitset for charcodes 0-255 
	u8 bitset[]
} cma_op_set

ranges are implemented as sets internally

{
	type = CMA_AND 
	CMA_OR
	flags = CMA_OWN // should copy seq object and alloc it inside opcode?
	
	size_t seq_len
	cma_op* seq;
	
	cma_op ops[]; // if OWN seq points here, else to some external memory
} cma_op_seq

{
	type = cma_op
	uint32_t min;
	uint32_t max; //0xFFFFFFFF - inf 
	
	cma_op pattern
	
} cma_op_rep;



typedef {
	uint8_t *str;
	size_t length;
	
	uint8_t* pos;
	uint8_t* end;
	
} cma_state;




// for now

dont use OWN, assume all memory is static from c file so no need to copy it
lets implement only basic matchign functionality first like match and repetitions
no caps and lookaheads

will also add refcnt later

#define S(str) cma_emit_str(str, 0 //CMA_OWN //) 
#define R(str) cma_emit_set(str) 


#define A(...) cma_emit_seq(CMA_AND, (static cma_op[]){__VA_ARGS} ) 
....
....
....


// for now sketch basic macros.
// convert pseudocode to real
// 
// keep everything not implemented as comments
// 
// make function to dump macros as ast bvy following seqs and other functions
