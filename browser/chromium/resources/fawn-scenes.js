// Fawn scenes: one frame uniform and shared sheet, three resident scenes, and a carrier that
// moves live material between them. Holds draw a scene natively; a scene change hands the
// outgoing state to the incoming scene and the carrier, then blends all three in linear light.
// While the logo forms, the finished image is handed to the mark, which moves it into the Fawn.
import { FRAME_FLOATS, createImagePasses, createKit } from './fawn-gpu.js';
import { createCarrier } from './fawn-carrier.js';
import { createFlow } from './fawn-flow.js';
import { createMagnets } from './fawn-magnets.js';
import { createMark } from './fawn-mark.js';
import { PARTICLE_POLICY, createParticles } from './fawn-particles.js';

const SCENE_POLICY = Object.freeze({
  maxCount: 262144,
  particleBytes: PARTICLE_POLICY.strideBytes,
  scenes: Object.freeze({ flow: createFlow, wave: createMagnets, particles: createParticles }),
});

// The carrier peaks mid-change, while each native scene owns its own end of the blend.
function transitionWeights(blend) {
  const carried = Math.sin(Math.PI * blend) ** 2;
  return [(1 - carried) * (1 - blend), (1 - carried) * blend, carried];
}

function createScenes(device, canvas, format, count) {
  if (!FawnFormation.WORKLOAD.cycleCounts.includes(count) || count > SCENE_POLICY.maxCount) {
    throw new Error(`Unsupported Fawn workload: ${count}`);
  }
  const kit = createKit(device, format);
  const grid = Math.ceil(Math.sqrt(count));
  const cells = grid * grid;
  const carrySlot = (i) => ({
    color: kit.storage(`Fawn carried color ${i}`, cells * 16),
    motion: kit.storage(`Fawn carried motion ${i}`, cells * 16),
  });
  // Slot 0 holds the outgoing scene's material, slot 1 the incoming scene's.
  const shared = {
    kit, canvas, grid, count,
    frame: kit.uniform('Fawn frame', FRAME_FLOATS * 4),
    carry: [carrySlot(0), carrySlot(1)],
    images: createImagePasses(kit),
  };
  const scenes = Object.fromEntries(
    Object.entries(SCENE_POLICY.scenes).map(([name, create]) => [name, create(shared)]));
  const carrier = createCarrier(shared);
  const mark = createMark(shared);
  const frameData = new Float32Array(FRAME_FLOATS);
  let targets = [], image = null;

  function resize() {
    kit.release([...targets, image].filter(Boolean));
    targets = [0, 1, 2].map((i) => kit.texture(`Fawn transition image ${i}`, canvas.width, canvas.height));
    image = kit.texture('Fawn scene image', canvas.width, canvas.height);
    Object.values(scenes).forEach((scene) => scene.resize?.());
    carrier.resize();
    mark.resize(image);
  }

  function writeFrame({ cycle, dt, time, steps, shape, pointer, burst }) {
    frameData.set([
      grid, grid, 1 / grid, 1 / grid,
      ...FawnFormation.viewport(canvas), canvas.width / canvas.height, shape,
      dt / steps, time, steps, cycle.blend,
      pointer.x, pointer.y, pointer.dx, pointer.dy,
      Number(pointer.hover), Number(pointer.down), pointer.press, cycle.interactionScale,
      burst.x, burst.y, burst.radius, burst.strength * cycle.interactionScale,
    ]);
    device.queue.writeBuffer(shared.frame, 0, frameData);
  }

  function draw(encoder, view, state) {
    writeFrame(state);
    const { cycle } = state;
    const outgoing = scenes[cycle.effect], incoming = scenes[cycle.next];
    if (cycle.started) {
      outgoing.exportCarry(encoder, 0);
      incoming.importCarry(encoder, 0);
      carrier.begin(encoder, outgoing, incoming);
    }
    if (cycle.transitioning) {
      outgoing.exportCarry(encoder, 0);
      incoming.exportCarry(encoder, 1);
    }
    if (state.dt > 0) {
      const pass = encoder.beginComputePass({ label: 'Fawn simulation' });
      outgoing.simulate(pass, state);
      if (cycle.transitioning) {
        incoming.simulate(pass, state);
        carrier.advect(pass);
      }
      pass.end();
    }
    const output = state.shape > 0 ? image : view;
    if (!cycle.transitioning) outgoing.render(encoder, output);
    else {
      outgoing.render(encoder, targets[0]);
      incoming.render(encoder, targets[1]);
      carrier.render(encoder, targets[2], cycle.blend);
      shared.images.compose(encoder, output, targets, transitionWeights(cycle.blend));
    }
    if (state.shape > 0) mark.draw(encoder, view, state.shape);
  }

  resize();
  return { resize, draw, destroy: () => kit.destroy() };
}

export { SCENE_POLICY, createScenes, transitionWeights };
