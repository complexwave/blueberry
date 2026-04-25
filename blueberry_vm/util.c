static uint8_t *bb_read_file(const char *path, uint32_t *out_len) {
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;

	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return NULL;
	}

	uint8_t *buf = b_malloc((uint32_t)st.st_size);
	ssize_t n = read(fd, buf, st.st_size);
	close(fd);

	if (n != st.st_size) {
		free(buf);
		return NULL;
	}

	*out_len = (uint32_t)st.st_size;
	return buf;
}
 
