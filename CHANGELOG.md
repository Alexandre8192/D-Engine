## [v0.2.0] Memory subsystem stabilized
- Documented the exception boundary limited to global new/delete.
- Ensured global operator new/delete overloads cover all aligned/nothrow forms.
- Guarded allocator logging by explicit log-level checks.
- Confirmed FrameAllocator's Reallocate signature matches IAllocator.
- Added new_delete smoke to the smoke suite.
