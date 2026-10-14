// Completed-frame rate: a label plus a short history graph.
const METER_POLICY = Object.freeze({ sampleS: 0.5, samples: 60, maxDpr: 2, floorFps: 60, stroke: '#ffffff80' });
const M = METER_POLICY;

function createMeter(label, graph) {
  const context = graph.getContext('2d');
  const history = [];
  let last = 0, frames = 0, seconds = 0;
  function plot() {
    const dpr = Math.min(devicePixelRatio, M.maxDpr);
    graph.width = Math.round(graph.clientWidth * dpr);
    graph.height = Math.round(graph.clientHeight * dpr);
    context.clearRect(0, 0, graph.width, graph.height);
    context.strokeStyle = M.stroke;
    context.lineWidth = dpr;
    context.beginPath();
    const ceiling = Math.max(M.floorFps, ...history);
    history.forEach((fps, i) => {
      const x = (i / (M.samples - 1)) * graph.width, y = graph.height * (1 - fps / ceiling);
      if (i) context.lineTo(x, y);
      else context.moveTo(x, y);
    });
    context.stroke();
  }
  return {
    // A restart drops the in-progress sample so pauses and hidden tabs never count as slow frames.
    restart(text = '— fps') {
      last = frames = seconds = 0;
      label.textContent = text;
    },
    clear() {
      history.length = 0;
      this.restart();
      plot();
    },
    complete(now) {
      if (last) {
        seconds += (now - last) / 1000;
        frames++;
      }
      last = now;
      if (seconds < M.sampleS) return;
      const fps = frames / seconds;
      label.textContent = `${Math.round(fps)} fps`;
      history.push(fps);
      if (history.length > M.samples) history.shift();
      frames = seconds = 0;
      plot();
    },
  };
}

export { METER_POLICY, createMeter };
