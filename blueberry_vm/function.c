

bb_cached_op* bb_function_ops(bb_function *fn){
	if(fn->ops) {
		return fn->ops;
	}
	
	fn->ops = bb_build_cached(fn); 
	
	return fn->ops;
}




