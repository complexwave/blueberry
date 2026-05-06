/*
 * blueberry_vm/lib/io.c — basic file I/O stdlib
 *
 * Functions:
 *   file_read(path)           → string (file contents) or null on error
 *   file_write(path, data)    → true on success, null on error
 *   file_append(path, data)   → true on success, null on error
 *   file_exists(path)         → true/false
 *   file_size(path)           → int (bytes) or null on error
 *   file_remove(path)         → true on success, null on error
 *   file_rename(old, new)     → true on success, null on error
 *   dir_list(path)            → array of strings, or null on error
 *   dir_exists(path)          → true/false
 *   dir_create(path)          → true on success, null on error
 *   exit(code)                → terminates process with exit code
 */

#include <dirent.h>
#include <sys/stat.h>

/* ---- helper: extract null-terminated path from ci_str ---- */

static char *bb_io_cpath(ci_ptr s, char *buf, size_t bufsz) {
	size_t len = ci_str_len(s);
	if (len >= bufsz) len = bufsz - 1;
	memcpy(buf, ci_str_head(s), len);
	buf[len] = '\0';
	return buf;
}

/* ---- file_read(path) ---- */

static ci_ptr bb_io_file_read(bb_coro_arg *c, ci_ptr path, ci_ptr a1, ci_ptr a2) {
	(void)c; (void)a1; (void)a2;
	BB_CHECK_STRING(path);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	int fd = open(pbuf, O_RDONLY);
	if (fd < 0)
		return NULL;

	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return NULL;
	}

	size_t sz = (size_t)st.st_size;
	ci_str *s = ci_str_new(sz);
	if (!s) {
		close(fd);
		return NULL;
	}

	uint8_t *dst = ci_str_ensure_tail(s, sz);
	ssize_t n = read(fd, dst, sz);
	close(fd);

	if (n < 0 || (size_t)n != sz) {
		ci_dec(s);
		return NULL;
	}

	ci_str_put_tail(s, sz);
	return (ci_ptr)s;
}

/* ---- file_write(path, data) ---- */

static ci_ptr bb_io_file_write(bb_coro_arg *c, ci_ptr path, ci_ptr data, ci_ptr a2) {
	(void)c; (void)a2;
	BB_CHECK_STRING(path);
	BB_CHECK_STRING(data);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	int fd = open(pbuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return NULL;

	size_t len = ci_str_len(data);
	ssize_t n = write(fd, ci_str_head(data), len);
	close(fd);

	if (n < 0 || (size_t)n != len)
		return NULL;

	return CI_BOOL(1);
}

/* ---- file_append(path, data) ---- */

static ci_ptr bb_io_file_append(bb_coro_arg *c, ci_ptr path, ci_ptr data, ci_ptr a2) {
	(void)c; (void)a2;
	BB_CHECK_STRING(path);
	BB_CHECK_STRING(data);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	int fd = open(pbuf, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return NULL;

	size_t len = ci_str_len(data);
	ssize_t n = write(fd, ci_str_head(data), len);
	close(fd);

	if (n < 0 || (size_t)n != len)
		return NULL;

	return CI_BOOL(1);
}

/* ---- file_exists(path) ---- */

static ci_ptr bb_io_file_exists(bb_coro_arg *c, ci_ptr path, ci_ptr a1, ci_ptr a2) {
	(void)c; (void)a1; (void)a2;
	BB_CHECK_STRING(path);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	struct stat st;
	return CI_BOOL(stat(pbuf, &st) == 0 && S_ISREG(st.st_mode));
}

/* ---- file_size(path) ---- */

static ci_ptr bb_io_file_size(bb_coro_arg *c, ci_ptr path, ci_ptr a1, ci_ptr a2) {
	(void)c; (void)a1; (void)a2;
	BB_CHECK_STRING(path);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	struct stat st;
	if (stat(pbuf, &st) < 0)
		return NULL;

	return CI_PACKINT((intptr_t)st.st_size);
}

/* ---- file_remove(path) ---- */

static ci_ptr bb_io_file_remove(bb_coro_arg *c, ci_ptr path, ci_ptr a1, ci_ptr a2) {
	(void)c; (void)a1; (void)a2;
	BB_CHECK_STRING(path);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	return CI_BOOL(unlink(pbuf) == 0);
}

/* ---- file_rename(old, new) ---- */

static ci_ptr bb_io_file_rename(bb_coro_arg *c, ci_ptr oldp, ci_ptr newp, ci_ptr a2) {
	(void)c; (void)a2;
	BB_CHECK_STRING(oldp);
	BB_CHECK_STRING(newp);

	char obuf[4096], nbuf[4096];
	bb_io_cpath(oldp, obuf, sizeof(obuf));
	bb_io_cpath(newp, nbuf, sizeof(nbuf));

	return CI_BOOL(rename(obuf, nbuf) == 0);
}

/* ---- dir_list(path) → array of filename strings ---- */

static ci_ptr bb_io_dir_list(bb_coro_arg *c, ci_ptr path, ci_ptr a1, ci_ptr a2) {
	(void)c; (void)a1; (void)a2;
	BB_CHECK_STRING(path);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	DIR *d = opendir(pbuf);
	if (!d)
		return NULL;

	ci_array *arr = ci_arr_new(32);
	struct dirent *ent;

	while ((ent = readdir(d)) != NULL) {
		/* skip . and .. */
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
		    (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;

		size_t nlen = strlen(ent->d_name);
		ci_str *name = ci_str_new(nlen);
		ci_str_append(name, (const uint8_t *)ent->d_name, nlen);
		ci_arr_push(arr, (ci_ptr)name);
	}

	closedir(d);
	return (ci_ptr)arr;
}

/* ---- dir_exists(path) ---- */

static ci_ptr bb_io_dir_exists(bb_coro_arg *c, ci_ptr path, ci_ptr a1, ci_ptr a2) {
	(void)c; (void)a1; (void)a2;
	BB_CHECK_STRING(path);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	struct stat st;
	return CI_BOOL(stat(pbuf, &st) == 0 && S_ISDIR(st.st_mode));
}

/* ---- dir_create(path) ---- */

static ci_ptr bb_io_dir_create(bb_coro_arg *c, ci_ptr path, ci_ptr a1, ci_ptr a2) {
	(void)c; (void)a1; (void)a2;
	BB_CHECK_STRING(path);

	char pbuf[4096];
	bb_io_cpath(path, pbuf, sizeof(pbuf));

	return CI_BOOL(mkdir(pbuf, 0755) == 0);
}

/* ---- exit(code) ---- */

static ci_ptr bb_io_exit(bb_coro_arg *c, ci_ptr code, ci_ptr a1, ci_ptr a2) {
	(void)c; (void)a1; (void)a2;

	int status = 0;
	if (code)
		status = (int)CI_INT(code);

	exit(status);
	return NULL; /* unreachable */
}

/* ---- registration ---- */

static void bb_lib_io_init(bb_vm *vm) {
	static const struct { const char *name; bb_cfn fn; } io_lib[] = {
		{ "file_read",   (bb_cfn)bb_io_file_read   },
		{ "file_write",  (bb_cfn)bb_io_file_write  },
		{ "file_append", (bb_cfn)bb_io_file_append },
		{ "file_exists", (bb_cfn)bb_io_file_exists },
		{ "file_size",   (bb_cfn)bb_io_file_size   },
		{ "file_remove", (bb_cfn)bb_io_file_remove },
		{ "file_rename", (bb_cfn)bb_io_file_rename },
		{ "dir_list",    (bb_cfn)bb_io_dir_list    },
		{ "dir_exists",  (bb_cfn)bb_io_dir_exists  },
		{ "dir_create",  (bb_cfn)bb_io_dir_create  },
		{ "exit",        (bb_cfn)bb_io_exit        },
	};

	for (size_t i = 0; i < sizeof(io_lib) / sizeof(io_lib[0]); i++) {
		bb_closure *cl = bb_vm_native(vm, io_lib[i].name, io_lib[i].fn);
		ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);
	}
}
