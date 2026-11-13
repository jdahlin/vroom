/* Writing the output file, and the parallel build-id hash chunk. */

static int write_output(void)
{
	/* replace, don't truncate in place: the old file may be mmap'ed by a reader */
	unlinkat(dirfd_base, opt.out, 0);
	int fd = openat(dirfd_base, opt.out, O_WRONLY | O_CREAT | O_TRUNC, opt.relocatable ? 0666 : 0777);
	if (fd < 0) {
		error("cannot open output file %s: %s", opt.out, strerror(errno));
		return 1;
	}
	uint64_t off = 0;
	while (off < ctx->layout.file_size) {
		ssize_t w = write(fd, out + off, ctx->layout.file_size - off > (1 << 30) ? (1 << 30) : ctx->layout.file_size - off);
		if (w <= 0) {
			error("write %s: %s", opt.out, strerror(errno));
			close(fd);
			return 1;
		}
		off += w;
	}
	close(fd);
	return 0;
}

static void hash_chunk(void *arg, size_t i)
{
	uint8_t *hashes = arg;
	uint64_t b = (uint64_t)i << 20, n = ctx->layout.file_size - b < (1u << 20) ? ctx->layout.file_size - b : (1u << 20);
	vdigest_blake3(out + b, n, hashes + i * 20, 20);
}
