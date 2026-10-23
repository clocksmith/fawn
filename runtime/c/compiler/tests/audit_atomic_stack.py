#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Complete atomic-owner graph, with every actual SCC member and old ceilings."""
import audit_m13_stack as audit

if __name__ == '__main__':
    ceiling, members = audit.FAMILIES['semantic_expression']
    audit.FAMILIES['semantic_expression'] = (
        ceiling, members | {'sema_image_call', 'sema_atomic_call', 'sema_address_reference'})
    audit.main()
