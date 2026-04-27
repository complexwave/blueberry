
this is pseudocode draft

bb_coro_*

enum {
	BB_CORO_RUNNING   = (1 << 0),
	BB_CORO_SUSPENDED = (1 << 1),
	BB_CORO_DONE      = (1 << 2),
	BB_CORO_ERROR     = (1 << 3),
	BB_CORO_RETURNED  = (1 << 4)
};


bb_coro_error(coro, "str")


ci_str bb_coro_state2str(vm, state)
	return bb_vm_cconst(vm, "RUNNING"),
	"SUSPENDED" .... 

bb_coro_runnable(coro) 
	state == SUSPENDED - yielded
	
	
	
bb_coro_new()
	coro starts in suspended state
	

	
bb_coro_push_call(coro)
	current push call logic

// exec function from c
bb_coro_exec(coro)
	state = suspended and fstack = 0
	or coro returned then set state = suspended - reuse coro
		else bb_vm_error("cant push call into running coro")
			
	bb_coro_push_call(coro)
	bb_coro_resume(coro, ) 
	


bb_coro__resume(coro, a,b,c)

bb_coro_resume(coro, 0-3 args) // a va args macro that calls
bb_coro__resume(coro, arg, NULL, NULL) // NULL for unused args

bb_coro_resume_args(coro, ci_arr) // dont implement, leave emopty for now. varargs one

bb_coro__yield(coro) simialr args thingy



// now calling convention is a mess

language is optimizied for c function common case - up to 3 args and 1 return
but supports varargs varrets. decided to not do compelx stack tricks like lua does.

native bytecode functions:
if var args specified, will get tmp array object as last arg.
else unnedeed args will be discarded

native functions return opcode has own list of registers, it doesnt copy to end of stack.
it might be more logical to implement return as just move copy to end of own stack and then copy back 
but this is unnecessary shufling around

return is just copy
caller[ regs_list++] = calle[rets_list++] in loop

but, when caller is c function it gets complicated.
for c covenience we should put returns as sequential list somewhere

__coro_return - this should be used for opcode

	if caller->type  = c_function
		goto leave_on_stack

	if caller->type  = bytecode
		
		if caler doesnt want varrets
			current logic, return from regs to regs directly
			
			coro->va_tail = NULL;
		else  // in language this looks like [call()] - and varargs calls like  something(a, call(), b) will be implementing using tmp arrays internally. so only [call()] needs to be supported, no weird stack offset tricks lua style
			leave_on_stack:
			
			copy things after our stack frame.
			
			[caller stack regs]
			[callee - current function, stack regs]
			rets_ptr -> [ copy rets here]
			
			coro->va_tail =  rets_ptr;
		end
		

CALLER is reponsible for cleaning callee stack, this is to keeps varrets and stuff consistent

coro_pop_frame()
	decr all regs of current frame and set to NULL
	pop frame
	
	

	
__call_c_enter_fixed is called after pushcall places calle at end of stack

__call_c_enter_fixed(coro, a,b,c) // enter call from C
	calle // function being called now
	calle->is_c
		if not vararg 
			return calle->cfunc(coro, a,b,c)
		end 
		
	ci_ptr pseudostack[] = {a,b,c}
	n = !!a + !!b + !!c // branchless lol
	
	__call_c_enter_varargs(coro, pseudostack, n)
	

__call_c_enter_varargs(coro, ci_ptr* args, n)
	if c_function 
			is vararg
			return calle->cfunc(vm, n, pseudostack)
			not var arg 
				err - how u got here?
			
	is native
		_copy_args_to_callee(coro, pseudostack, n)
		
	
		
_copy_args_to_calle()
	while n and calle args < cur
		copy arg to calle stack
		
	if callee is varargs // a flag on a function
		ci_array = new()
		push tail to array
	
	push array to args_cnt + 1 register - calle true prototype is  call(a,b,c,d, _args) and it expects ci array as _args


call opcode has own code similar to _copy_args_to_calle
as it copies using indices from opcode itself instead of contigious array


resume should support calling both native and c functions and optionally accept varargs
ideally code should be unified with call opcode




