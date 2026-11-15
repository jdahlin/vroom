/*
 * @FILE response files, as GNU ld, lld and llvm-ar read them.
 *
 *   vresponse_files_expand(&argc, &argv, dirfd, &keep)
 *       replaces each @FILE argument (after argv[0]) by FILE's words: blanks
 *       separate them; '...', "..." and \ quote; recursively, at most 64 files.
 *       FILE is relative to dirfd.  An @FILE that cannot be read stays as it is.
 *       Returns whether any argument started with @ (argc/argv unchanged if not).
 *       The new vector and its words are in keep's blocks.
 *   vresponse_files_free(keep)
 *       frees them.
 */
#ifndef VCC_COMMON_RESPONSE_FILES_H
#define VCC_COMMON_RESPONSE_FILES_H

#include <stdbool.h>

struct vresponse_memory {
    struct vresponse_memory *next;
};

bool vresponse_files_expand(int *pargc, char ***pargv, int dirfd,
                            struct vresponse_memory **keep);
void vresponse_files_free(struct vresponse_memory *keep);

#endif
