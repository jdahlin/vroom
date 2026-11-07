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

struct vroom_req_hdr {
	uint32_t magic;
	uint32_t argc;
	uint32_t envc;
	uint32_t payload_len;
};

#endif
