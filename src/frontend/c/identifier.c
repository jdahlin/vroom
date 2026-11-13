/* vcc front end: spelling of an interned identifier */
#include "ccimpl.h"

VPRIV const char *idname(uint32_t id, int *len)
{
	*len = (int)vintern_ents[id].len;
	return vintern_ents[id].s;
}
VPRIV int idlen_(uint32_t id) { return (int)vintern_ents[id].len; }
VPRIV const char *idstr_(uint32_t id) { return vintern_ents[id].s; }
