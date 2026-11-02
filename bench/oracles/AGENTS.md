# Independent expected outputs

Inherit [benchmark instructions](../AGENTS.md). Own CPU references, numerical
acceptance and expected-output artifacts independently of the timed implementation.

Bind input bytes, shape, dtype and the declared exactness/tolerance contract.
Never compute expectations through the same GPU path under test or weaken a
frozen oracle after a loss. Report unsupported reference cases explicitly.
Run affected oracle controls, including an incorrect-output rejection case.
