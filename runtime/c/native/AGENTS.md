# Public C native compute frontend

This owner converts the pinned public WebGPU ABI and orchestrates existing owners.
Do not create a second resource reference count, command store, callback queue,
compiler parser or backend. Device and adapter references belong here; resource
handles alias the resource owner, and instance handles alias the accepted instance.
Never call a foreign callback under a device mutex. A pending backend submission
keeps every resource lease until proven terminal. Unimplemented exports remain
absent; do not generate placeholder success symbols to satisfy a symbol inventory.
