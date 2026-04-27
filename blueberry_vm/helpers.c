
ci_str bb_vm_cconst(vm, const char* c static string)
	later will contain fast table to return cached strings by c string ptr fast, but for now just use vm_istring to lookup in interned strings table 
