// The raw OS result of Node's forced process termination, without inventing
// POSIX signals on Windows. Recovery expectations are shared across platforms.
function abruptExitMatches(exit, platform = process.platform) {
  switch (platform) {
    case 'win32': return exit?.code === 1 && exit.signal === null;
    case 'linux':
    case 'darwin': return exit?.code === null && exit.signal === 'SIGKILL';
    default: return false;
  }
}

function observedKillRequest(request) {
  return request?.signal === 'SIGKILL' && Number.isInteger(request.pid) && request.pid > 0
    && ['controller', 'host-hook'].includes(request.source);
}

function terminationEvidence(exit, request) {
  if (!abruptExitMatches(exit) || !observedKillRequest(request)) {
    throw new Error(`forced termination lacks an observed request/OS exit: ${JSON.stringify({ exit, request })}`);
  }
  return { requestedSignal: request.signal, abrupt: true, hostPid: request.pid, request, rawExit: exit };
}

function terminationMatches(evidence, platform) {
  return evidence?.abrupt === true && observedKillRequest(evidence.request)
    && evidence.requestedSignal === evidence.request.signal && evidence.hostPid === evidence.request.pid
    && abruptExitMatches(evidence.rawExit, platform);
}

export { abruptExitMatches, terminationEvidence, terminationMatches };
