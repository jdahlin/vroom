/* vroom: protokoll mellan vroom-cc (klient) och vroomd (server).
 *
 * Klienten ansluter till en AF_UNIX/SOCK_STREAM-socket och skickar ETT meddelande
 * med sendmsg(): en header + nyttolast, och fd 0,1,2 som SCM_RIGHTS.
 * Nyttolasten är NUL-separerade strängar: cwd, argv[0..argc-1], envp[0..envc-1].
 * Servern svarar med en int32: exitstatus (0–255), eller 128+signal.
 */
#ifndef VROOM_PROTOCOL_H
#define VROOM_PROTOCOL_H
#include <stdint.h>

#define VROOM_MAGIC 0x6d6f6f72u /* "room" */
#define VROOM_MAX_PAYLOAD (1u << 20)
/* vccd's answer to a vsh request it leaves to the real shell (not an exit status) */
#define VROOM_STATUS_DECLINE 0x7fff0000

struct vroom_req_hdr {
	uint32_t magic;
	uint32_t argc;
	uint32_t envc;
	uint32_t payload_len;
};

/* the default socket: in a per-user 0700 directory, never a shared /tmp name.
 * $XDG_RUNTIME_DIR/vccd/vccd.sock, else ${TMPDIR:-/tmp}/vccd-UID/vccd.sock.
 * The directory goes to dir (created 0700 when make), the socket path to sock.
 * Returns 0, or -1 when the directory is missing, not ours or open to others. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
static inline int vroom_default_sock(char *dir, size_t dcap, char *sock, size_t scap, int make)
{
	const char *x = getenv("XDG_RUNTIME_DIR"), *t = getenv("TMPDIR");
	struct stat st;

	if (x && *x)
		snprintf(dir, dcap, "%s/vccd", x);
	else
		snprintf(dir, dcap, "%s/vccd-%u", t && *t ? t : "/tmp", (unsigned)getuid());
	snprintf(sock, scap, "%s/vccd.sock", dir);
	if (make)
		mkdir(dir, 0700);
	if (lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077))
		return -1;
	return 0;
}

#endif
