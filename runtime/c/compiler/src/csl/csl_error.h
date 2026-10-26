// SPDX-License-Identifier: MIT
#ifndef DOE_CSL_ERROR_H
#define DOE_CSL_ERROR_H
typedef enum CslErr {
    CSL_OK = 0,
    CSL_ERR_INVALID = 1,
    CSL_ERR_UNSUPPORTED = 2,
    CSL_ERR_TOO_LARGE = 3,
    CSL_ERR_NOMEM = 4
} CslErr;
#endif
