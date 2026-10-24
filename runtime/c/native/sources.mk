# SPDX-License-Identifier: MIT
# Owner directories feed the same native source and include discovery on every host.
NATIVE_OWNER_DIRS = ../instance ../futures ../callbacks ../resources ../labels ../error-scopes ../shader ../backends/vulkan ../backends/metal ../platform
# Real private D3D provider objects are linked only by the Windows SDK build.
NATIVE_WINDOWS_OWNER_DIRS = ../backends/d3d12
