/* Writing the output file, and the parallel build-id hash chunk. */

static int ld_output_file_write(void)
{
	/* replace, don't truncate in place: the old file may be mmap'ed by a reader */
	unlinkat(ctx->options.directory_fd, ctx->options.out, 0);
	int fd = openat(ctx->options.directory_fd, ctx->options.out, O_WRONLY | O_CREAT | O_TRUNC, ctx->options.relocatable ? 0666 : 0777);
	if (fd < 0) {
		ld_error_report("cannot open output file %s: %s", ctx->options.out, strerror(errno));
		return 1;
	}
	uint64_t off = 0;
	while (off < ctx->layout.file_size) {
		ssize_t w = write(fd, ctx->output.image + off, ctx->layout.file_size - off > (1 << 30) ? (1 << 30) : ctx->layout.file_size - off);
		if (w <= 0) {
			ld_error_report("write %s: %s", ctx->options.out, strerror(errno));
			close(fd);
			return 1;
		}
		off += w;
	}
	close(fd);
	return 0;
}

static void ld_build_id_chunk_hash(void *arg, size_t i)
{
	uint8_t *hashes = arg;
	uint64_t b = (uint64_t)i << 20, n = ctx->layout.file_size - b < (1u << 20) ? ctx->layout.file_size - b : (1u << 20);
	vdigest_blake3(ctx->output.image + b, n, hashes + i * 20, 20);
}
