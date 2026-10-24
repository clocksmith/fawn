#!/usr/bin/env bun
// Independent N-API environments in Bun workers against one retained native
// library: the Node entrypoint, run by Bun (its workers load the same addon).
import './test-integration-native-concurrent-devices.js';
