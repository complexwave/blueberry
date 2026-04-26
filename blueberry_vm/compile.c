static uint8_t *bb_compile_ci_file(const char *ci_path, uint32_t *out_len) {
	b_parser *p = b_parser_new();
	if (!b_parser_load_file(p, ci_path)) {
		fprintf(stderr, "error: cannot read '%s'\n", ci_path);
		free(p);
		return NULL;
	}

	ast *a = ast_new(p);
	ast_node *block = ast_codelist(a);
	if (!block) {
		fprintf(stderr, "error: parse failed\n");
		ast_free(a);
		b_parser_free(p);
		return NULL;
	}

	/* codegen */
	b_unit *unit = b_unit_new();
	char *main_name = b_malloc(5);
	memcpy(main_name, "main", 5);
	b_function *main_fn = b_function_new(unit, main_name);
	b_codeblock *cb = b_codeblock_new(main_fn, NULL);
	main_fn->cb = cb;
	b_consume_codelist(cb, block);

	/* IR encode pass */
	uint32_t fcnt = ci_arr_len(unit->functions);
	for (uint32_t fi = 0; fi < fcnt; fi++) {
		b_function *f = (b_function *)ci_arr_index(unit->functions, fi);
		b_encode(f->cb);
	}

	/* binary encode */
	bc_buf *binary = be_encode_unit(unit);

	uint32_t blen = bc_buf_len(binary);
	uint8_t *result = b_malloc(blen);
	memcpy(result, bc_buf_data(binary), blen);
	*out_len = blen;

	/* cleanup */
	ast_node_free(block);
	ast_free(a);
	b_parser_free(p);

	return result;
} 
