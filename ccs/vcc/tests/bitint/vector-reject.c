/* Scalar support does not imply support for wide vector lanes. */
typedef _BitInt(256) signed_vector __attribute__((vector_size(64)));
typedef unsigned _BitInt(129) unsigned_vector __attribute__((vector_size(64)));
typedef _BitInt(310) extended_vector __attribute__((ext_vector_type(2)));
typedef _BitInt(256) *pointer_to_vector __attribute__((vector_size(64)));
