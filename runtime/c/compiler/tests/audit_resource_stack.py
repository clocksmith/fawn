#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""ABI6 complete graph audit; adds the actual image SCC member, no new ceiling."""
import audit_m13_stack as audit

if __name__ == '__main__':
    ceiling, members = audit.FAMILIES['semantic_expression']
    audit.FAMILIES['semantic_expression'] = (ceiling, members | {'sema_image_call'})
    audit.main()
