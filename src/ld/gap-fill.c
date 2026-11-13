/* The script's =fill patterns between and after input sections. */

/* =fill: lld fills before, between and after the input sections, restarting the pattern per gap */
static void fill_gap(uint8_t *b, uint64_t n, const uint8_t f[4])
{
	uint64_t i = 0;
	for (; i + 4 < n; i += 4)
		memcpy(b + i, f, 4);
	memcpy(b + i, f, n - i);
}
static void write_fills(void)
{
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		OSec *os = &osecs.v[topcmds.v[c].idx];
		/* without =FILL lld pads executable sections with the target's trap
		   instruction: int3 on x86-64 */
		bool trap = !os->fill && x86_ld && (os->flags & SHF_EXECINSTR);
		if (!os->live || (!os->fill && !trap) || os->type == SHT_NOBITS)
			continue;
		uint32_t v = trap ? 0xccccccccu : (uint32_t)ev_value(eval(os->fill));
		if (!v)
			continue;
		uint8_t f[4] = {v >> 24, v >> 16, v >> 8, v};	/* big-endian (lld readFill) */
		uint8_t *b = out + os->offset;
		uint64_t at = 0;
		for (uint32_t k = 0; k < os->cmds.n; k++) {
			if (os->cmds.v[k].kind != C_ISD)
				continue;
			ISD *d = &isds.v[os->cmds.v[k].idx];
			for (uint32_t q = 0; q < d->secs.n; q++) {
				ISec *is = &isecs.v[d->secs.v[q]];
				if (is->off > at)
					fill_gap(b + at, is->off - at, f);
				at = is->off + is->size;
			}
		}
		if (os->size > at)
			fill_gap(b + at, os->size - at, f);
	}
}
