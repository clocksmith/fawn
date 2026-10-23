"""Independent interleaved SPIR-V workgroups and Workgroup-only race model.

A scheduling quantum ends after a memory access or barrier. Pure register/local
instructions between those events cannot affect another invocation and commute.
Each invocation is a suspended interpreter with its own full stack, not replay.
"""
from __future__ import annotations
from dataclasses import dataclass
from itertools import product
import random
from target_m12 import Aggregate, Invalid, Module, Pointer, Unfinished


class Race(Invalid):
    pass


class Rendezvous(Invalid):
    pass


@dataclass(frozen=True)
class Access:
    invocation: int
    group: tuple
    epoch: int
    write: bool


class MemoryObserver:
    def __init__(self, module, buffers, epochs, mutation=None):
        self.module = module
        self.buffers = buffers
        self.epochs = epochs
        self.mutation = mutation
        self.history = {}
        self.total = 0
        self.locations = {}

    def cells(self, ptr):
        cache_key = (ptr.root, ptr.indices)
        if cache_key in self.locations:
            return self.locations[cache_key]
        m = self.module
        pointer_type, storage, _ = m.variables[ptr.root]
        typ = m.types[pointer_type][1][1]
        resource = m.resources.get(ptr.root)
        offset = 0
        path = ()
        vector = None
        for index in ptr.indices:
            opcode, args = m.types[typ]
            if opcode == 23 and vector is None:
                vector = (path, offset)
            child, count = m.children(typ)
            if opcode == 30:
                if resource is not None:
                    offset += m.member[(typ, index)][35][0]
                typ = child[index]
            else:
                if resource is not None:
                    offset += index * (4 if opcode == 23 else m.decor[typ][6][0])
                typ = child
            path += (index,)
        if vector is not None:
            result = [(resource, vector[1], vector[0])]
        else:
            result = []
            work = [(typ, offset, path)]
            while work:
                current, off, component = work.pop()
                opcode, args = m.types[current]
                if opcode in {20, 21, 22, 23}:
                    result.append((resource, off, component))
                elif opcode in {28, 30}:
                    child, count = m.children(current)
                    for i in range(count):
                        next_type = child[i] if opcode == 30 else child
                        delta = 0
                        if resource is not None:
                            delta = (m.member[(current, i)][35][0] if opcode == 30
                                     else i*m.decor[current][6][0])
                        work.append((next_type, off+delta, component+(i,)))
                else:
                    raise Invalid('race footprint of nonfixed object')
        self.locations[cache_key] = (storage, result)
        return storage, result

    def access(self, identity, ptr, write):
        invocation, group = identity
        storage, cells = self.cells(ptr)
        now = Access(invocation, group, self.epochs[group], write)
        for resource, offset, path in cells:
            if storage == 4:
                physical_group = (0, 0, 0) if self.mutation == 'alias_groups' else group
                if self.mutation == 'privatize_shared':
                    physical_group = ('private', invocation)
                key = ('workgroup', physical_group, ptr.root, path)
            else:
                key = ('storage', resource, offset)
            prior = self.history.setdefault(key, {'reads': {}, 'writes': {}, 'epoch': -1})
            # Clearing at a new Workgroup epoch is safe only without the deliberate
            # cross-group-alias control. Reads do not erase a prior unordered write.
            if storage == 4 and self.mutation != 'alias_groups' and prior['epoch'] != now.epoch:
                prior['reads'].clear()
                prior['writes'].clear()
                prior['epoch'] = now.epoch
            conflicts = list(prior['writes'].values())
            if write:
                conflicts += list(prior['reads'].values())
            for other in conflicts:
                if other.invocation == invocation:
                    continue
                ordered = storage == 4 and other.group == group and other.epoch < now.epoch
                if not ordered:
                    raise Race(f'unordered {key}: {other} versus {now}')
            prior['writes' if write else 'reads'][invocation] = now
            self.total += 1


def poison(module, typ, byte):
    tasks = [('visit', typ)]
    values = []
    while tasks:
        action, current = tasks.pop()
        if action == 'join':
            count = module.children(current)[1]
            values.append(Aggregate(current, tuple(values[-count:])))
            if count:
                del values[-count-1:-1]
            continue
        opcode, args = module.types[current]
        if opcode == 20:
            values.append(True)
        elif opcode in {21, 22}:
            values.append(byte*0x01010101)
        elif opcode in {23, 28, 30}:
            child, count = module.children(current)
            tasks.append(('join', current))
            tasks.extend(('visit', child[i] if opcode == 30 else child)
                         for i in reversed(range(count)))
        else:
            raise Invalid('nonconstructible Workgroup poison')
    if len(values) != 1:
        raise Invalid('poison assembly')
    return values[0]


def dispatch(module, buffers, groups=(1, 1, 1), schedule='round_robin',
             poison_byte=0xa5, mutation=None, exact_barriers=True,
             event_limit=100000000, pure_quantum=64, shared_backing=None):
    if module.local is None:
        raise Invalid('missing local size')
    group_ids = list(product(*(range(x) for x in groups)))
    epochs = {group: 0 for group in group_ids}
    observer = MemoryObserver(module, buffers, epochs, mutation)
    roots = {rid: module.types[ptr][1][1]
             for rid, (ptr, storage, _) in module.variables.items() if storage == 4}
    shared = {} if shared_backing is None else shared_backing
    refreshed = set()
    tasks = {}
    task_group = {}
    groups_members = {group: [] for group in group_ids}
    for group in group_ids:
        physical_group = (0, 0, 0) if mutation == 'alias_groups' else group
        if physical_group not in refreshed:
            memory = shared.setdefault(physical_group, {})
            memory.clear()
            memory.update({rid: poison(module, typ, poison_byte)
                           for rid, typ in roots.items()})
            refreshed.add(physical_group)
        # x is fastest varying local index, matching WGSL's invocation mapping.
        for z in range(module.local[2]):
            for y in range(module.local[1]):
                for x in range(module.local[0]):
                    inv = len(tasks)
                    index = x+module.local[0]*(y+module.local[1]*z)
                    local = (x, y, z)
                    global_id = tuple(group[i]*module.local[i]+local[i] for i in range(3))
                    builtins = {24: groups, 26: group, 27: local, 28: global_id, 29: index}
                    memory = shared[physical_group]
                    if mutation == 'privatize_shared':
                        memory = {rid: poison(module, typ, poison_byte) for rid, typ in roots.items()}
                    tasks[inv] = module.invocation(buffers, builtins, memory, observer, (inv, group), pure_quantum=pure_quantum)
                    task_group[inv] = group
                    groups_members[group].append(inv)
    pending = {}
    finished = {}
    generator = random.Random(0x11c0ffee)
    sweeps = 0
    events = 0
    barrier_counts = {group: 0 for group in group_ids}
    burst_pattern = (1, 3, 7)
    while len(finished) < len(tasks):
        order = [i for i in tasks if i not in finished and i not in pending]
        if not order:
            raise Rendezvous('no runnable invocation; mismatched dynamic barrier or early exit')
        if schedule == 'reverse':
            order.reverse()
        elif schedule == 'shuffle_0x11c0ffee':
            generator.shuffle(order)
        elif schedule not in {'round_robin', 'bounded_burst_1_3_7'}:
            raise ValueError('unknown schedule '+schedule)
        for inv in order:
            if inv in finished or inv in pending:
                continue
            burst = burst_pattern[sweeps % 3] if schedule == 'bounded_burst_1_3_7' else 1
            for _ in range(burst):
                try:
                    event = next(tasks[inv])
                except StopIteration as end:
                    finished[inv] = end.value
                    break
                events += 1
                if events > event_limit:
                    raise Unfinished('interleaved observation event budget')
                if event[0] != 'barrier':
                    continue
                _, phase, scopes = event
                if exact_barriers and scopes != (2, 2, 0x108):
                    raise Invalid('barrier scopes/semantics must be exactly (2,2,0x108)')
                group = task_group[inv]
                if mutation == 'skip_lane_barrier' and inv == 1 and barrier_counts[group] == 1:
                    continue
                if mutation in {'helper_phase_mismatch', 'loop_phase_mismatch'} and inv == 1:
                    phase += (mutation,)
                pending[inv] = (phase, scopes)
                members = groups_members[group]
                if all(i in pending for i in members):
                    phases = {pending[i] for i in members}
                    if len(phases) != 1:
                        raise Rendezvous('different static/caller/loop phase at rendezvous')
                    if scopes[0] != 2:
                        raise Rendezvous('execution scope is not workgroup')
                    if scopes[1] == 2 and scopes[2] & 0x108 == 0x108:
                        epochs[group] += 1
                    barrier_counts[group] += 1
                    for i in members:
                        del pending[i]
                break
        for group, members in groups_members.items():
            if any(i in pending for i in members) and all(i in pending or i in finished for i in members):
                raise Rendezvous('barrier cannot rendezvous with exited invocation')
        sweeps += 1
    return {'events': events, 'steps': sum(x['steps'] for x in finished.values()),
            'barriers_per_group': {str(k): v for k, v in barrier_counts.items()},
            'memory_accesses': observer.total, 'invocations': len(tasks),
            'groups': len(group_ids), 'schedule': schedule, 'poison': poison_byte,
            'pure_instruction_quantum': pure_quantum,
            'caller_supplied_reusable_shared_storage': shared_backing is not None}
