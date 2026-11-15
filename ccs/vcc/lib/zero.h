#ifndef VCC_ZERO_H
#define VCC_ZERO_H

/* Initialize every field, including fields added later. */
#ifdef __cplusplus
#define VCC_ZERO_INIT                                                          \
    {                                                                          \
    }
#else
#define VCC_ZERO_INIT {0}
#endif

#endif
