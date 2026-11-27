-- DoeBuffer lifecycle state machine.
-- Abstract lifecycle model. Native mapped-range access also depends on map mode,
-- offsets and size. The predicates below have no runtime elimination binding.

/-- States in the lifecycle model. -/
inductive BufferState where
  | unmapped   -- alive and unmapped
  | mapped     -- alive, mapped for CPU access
  | released   -- terminal state after release
  deriving Repr, DecidableEq

-- Lifecycle transitions in the abstract model

/-- Mapping moves any live state to mapped. -/
def doeMapAsync : BufferState → BufferState
  | .released => .released
  | _ => .mapped

/-- Unmapping moves any live state to unmapped. -/
def doeUnmap : BufferState → BufferState
  | .released => .released
  | _ => .unmapped

/-- Releasing enters the terminal state. -/
def doeRelease : BufferState → BufferState
  | _ => .released

-- Core lifecycle properties

/-- Release is terminal: any state transitions to released. -/
theorem doeRelease_terminal (s : BufferState) :
    doeRelease s = .released := by
  cases s <;> rfl

/-- Released state absorbs all transitions. -/
theorem released_absorbs_all (s : BufferState) (hs : s = .released) :
    doeMapAsync s = .released ∧ doeUnmap s = .released ∧ doeRelease s = .released := by
  subst hs; exact ⟨rfl, rfl, rfl⟩

-- Idempotency

/-- mapAsync is idempotent. -/
theorem doeMapAsync_idempotent (s : BufferState) :
    doeMapAsync (doeMapAsync s) = doeMapAsync s := by
  cases s <;> rfl

/-- unmap is idempotent. -/
theorem doeUnmap_idempotent (s : BufferState) :
    doeUnmap (doeUnmap s) = doeUnmap s := by
  cases s <;> rfl

/-- map then unmap returns to unmapped (round-trip for live buffers). -/
theorem doeMapUnmap_roundtrip (s : BufferState) (hs : s ≠ .released) :
    doeUnmap (doeMapAsync s) = .unmapped := by
  cases s <;> simp_all [doeMapAsync, doeUnmap]

/-- unmap then map returns to mapped (reverse round-trip for live buffers). -/
theorem doeUnmapMap_roundtrip (s : BufferState) (hs : s ≠ .released) :
    doeMapAsync (doeUnmap s) = .mapped := by
  cases s <;> simp_all [doeMapAsync, doeUnmap]

-- Comparison of permissive and strict predicates over the modeled states

/-- Permissive mapped-range predicate over live buffers. -/
def doeAllowsGetMappedRange : BufferState → Bool
  | .released => false
  | _ => true

/-- Strict mapped-range predicate restricted to mapped buffers. -/
def specAllowsGetMappedRange : BufferState → Bool
  | .mapped => true
  | _ => false

/-- Permissive dispatch predicate over live buffers. -/
def doeAllowsDispatch : BufferState → Bool
  | .released => false
  | _ => true

/-- Strict dispatch predicate restricted to unmapped buffers. -/
def specAllowsDispatch : BufferState → Bool
  | .unmapped => true
  | _ => false

/-- The predicates differ on mapped-range access from the unmapped state. -/
theorem doe_getMappedRange_gap :
    doeAllowsGetMappedRange .unmapped = true ∧ specAllowsGetMappedRange .unmapped = false := by
  exact ⟨rfl, rfl⟩

/-- The predicates differ on dispatch from the mapped state. -/
theorem doe_dispatch_gap :
    doeAllowsDispatch .mapped = true ∧ specAllowsDispatch .mapped = false := by
  exact ⟨rfl, rfl⟩

/-- The permissive predicate includes every strict mapped-range state. -/
theorem doe_getMappedRange_superset :
    ∀ s : BufferState, specAllowsGetMappedRange s = true → doeAllowsGetMappedRange s = true := by
  intro s h
  cases s <;> simp_all [doeAllowsGetMappedRange, specAllowsGetMappedRange]

/-- The permissive predicate includes every strict dispatch state. -/
theorem doe_dispatch_superset :
    ∀ s : BufferState, specAllowsDispatch s = true → doeAllowsDispatch s = true := by
  intro s h
  cases s <;> simp_all [doeAllowsDispatch, specAllowsDispatch]
