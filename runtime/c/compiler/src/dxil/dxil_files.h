/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_FILES_H
#define DOE_DXIL_FILES_H

#include <stdio.h>

/* Private DXC temporary-file boundary. Paths are UTF-8 on every platform.
 * Creation is exclusive, binary, and non-inheritable; errors use errno.
 * The caller owns returned streams and any paths successfully created. */
#define DXIL_FILES_PATH_CAP 1024u

int dxil_files_mkdir(const char *path);
int dxil_files_rmdir(const char *path);
int dxil_files_remove(const char *path);
FILE *dxil_files_create(const char *path);
FILE *dxil_files_read(const char *path);
unsigned long dxil_files_process_id(void);

#endif /* DOE_DXIL_FILES_H */
