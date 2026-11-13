/* Writing the output file, and the parallel build-id hash chunk. */

static int ld_output_file_write(struct ld_context *ctx)
{
	/* replace, don't truncate in place: the old file may be mmap'ed by a reader */
	unlinkat(ctx->options.directory_fd, ctx->options.output_path, 0);
	int fd = openat(ctx->options.directory_fd, ctx->options.output_path, O_WRONLY | O_CREAT | O_TRUNC, ctx->options.relocatable ? 0666 : 0777);
	if (fd < 0) {
		ld_error_report(ctx, "cannot open output file %s: %s", ctx->options.output_path, strerror(errno));
		return 1;
	}
	uint64_t off = 0;
	while (off < ctx->layout.file_size) {
		ssize_t w = write(fd, ctx->output.image + off, ctx->layout.file_size - off > (1 << 30) ? (1 << 30) : ctx->layout.file_size - off);
		if (w <= 0) {
			ld_error_report(ctx, "write %s: %s", ctx->options.output_path, strerror(errno));
			close(fd);
			return 1;
		}
		off += w;
	}
	close(fd);
	return 0;
}

/* the image and where the chunk hashes go, for ld_build_id_chunk_hash */
typedef struct { const uint8_t *image; uint64_t size; uint8_t *hashes; } ld_build_id_job;

static void ld_build_id_chunk_hash(void *arg, size_t i)
{
	const ld_build_id_job *job = arg;
	uint64_t b = (uint64_t)i << 20, n = job->size - b < (1u << 20) ? job->size - b : (1u << 20);
	vdigest_blake3(job->image + b, n, job->hashes + i * 20, 20);
}
