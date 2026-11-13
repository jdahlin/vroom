/* vcc front end: spelling of an interned identifier */
#include "cc-internal.h"

CC_PRIVATE const char *cc_identifier_name(uint32_t id, int *len)
{
	*len = (int)vintern_ents[id].len;
	return vintern_ents[id].s;
}
CC_PRIVATE int cc_identifier_length(uint32_t id) { return (int)vintern_ents[id].len; }
CC_PRIVATE const char *cc_identifier_string(uint32_t id) { return vintern_ents[id].s; }
