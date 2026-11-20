"""GDB source script for a freshly launched, owned diagnostic process with matching Doe DWARF.

Read-only diagnostic: records native Vulkan compute, successful submission and fence/timeline observation.
The library digest is supplied out of band. This tool has no browser document or page ownership source.
"""

import hashlib
import json
import os
from pathlib import Path

import gdb

library = Path(os.environ["BLITZEN_NATIVE_TRACE_LIBRARY"]).resolve(strict=True)
expected = os.environ["BLITZEN_NATIVE_TRACE_SHA256"]
if hashlib.sha256(library.read_bytes()).hexdigest() != expected:
    raise gdb.GdbError("Native trace library differs from its independent artifact pin")
identity = library.stat()
output = Path(os.environ["BLITZEN_NATIVE_TRACE_OUT"])
log = output.open("x")
calls, by_command, queues, errors = {}, {}, {}, []
counter = queue_count = commands_seen = 0
retired_unresolved = set()
owner_pid = gdb.selected_inferior().pid
if not owner_pid or not any(Path(obj.filename).resolve() == library for obj in gdb.objfiles()):
    raise gdb.GdbError("Start the owned inferior and load the pinned Doe image before arming probes")


def emit(event, **fields):
    log.write(json.dumps({"event": event, **fields}) + "\n")
    log.flush()


def native_frame(frame):
    # Symbol name and a path alone would allow same-named probes in another image.
    pc = frame.pc()
    if gdb.selected_inferior().pid != owner_pid or gdb.solib_name(pc) != str(library):
        return False
    with open(f"/proc/{owner_pid}/maps") as maps:
        for line in maps:
            span, _, _, dev, inode, *_ = line.split(None, 5)
            start, end = [int(part, 16) for part in span.split("-")]
            if start <= pc < end:
                major, minor = (int(field, 16) for field in dev.split(":"))
                return int(inode) == identity.st_ino and os.makedev(major, minor) == identity.st_dev
    return False


class Entry(gdb.Breakpoint):
    def __init__(self, symbol, callback, source_suffix=None):
        # Match the symbol in the pinned objfile, ignoring identically named
        # Vulkan loaders, test program definitions and PLT entries.
        addresses = [
            sym
            for sym in gdb.lookup_static_symbols(symbol)
            if sym.symtab and sym.symtab.objfile.filename == str(library)
        ]
        if source_suffix:
            addresses = [sym for sym in addresses if sym.symtab.fullname().endswith(source_suffix)]
        if not addresses and not source_suffix:
            obj = next(o for o in gdb.objfiles() if o.filename == str(library))
            sym = obj.lookup_global_symbol(symbol)
            if sym is None:
                raise gdb.GdbError("Pinned library debug symbol absent: " + symbol)
            addresses = [sym]
        if len(addresses) != 1:
            raise gdb.GdbError("Ambiguous native symbol: " + symbol)
        address = int(addresses[0].value().address)
        super().__init__("*" + hex(address), internal=True)
        self.callback = callback

    def stop(self):
        try:
            frame = gdb.newest_frame()
            if not native_frame(frame):
                raise gdb.GdbError("Breakpoint outside pinned loaded image")
            self.callback(frame, gdb.selected_thread().global_num)
        except Exception as exc:
            errors.append(str(exc))
        return False


def public_parent(frame):
    while frame:
        if frame.name() == "wgpuQueueSubmit" and native_frame(frame):
            return frame
        frame = frame.older()
    return None


def public_submit(frame, thread):
    global counter, queue_count
    queue = int(frame.read_var("handle"))
    if not queue or not int(frame.read_var("count")):
        return
    counter += 1
    # Start a fresh mapping whenever this image begins constructing a device.
    # An old, still live queue may get a new ordinal then; a recycled address
    # can never borrow an older device's ordinal. Ordinals never authorize work.
    if queue not in queues:
        queue_count += 1
        queues[queue] = queue_count
    calls[thread] = (counter, queues[queue], queue)


def recorded(frame, thread):
    global commands_seen
    parent = public_parent(frame)
    if parent is None or thread not in calls:
        return
    cmd = int(frame.read_var("commands"))
    invocation = calls[thread]
    if invocation[2] != int(parent.read_var("handle")):
        errors.append("Public queue changed during compute encoding")
        return
    groups = [int(frame.read_var(axis)) for axis in ("x", "y", "z")]
    if not all(groups):
        return
    old = by_command.get(cmd)
    if old and old["invocation"] != invocation:
        errors.append("Native command lifetime collided across public submits")
        return
    if not old:
        commands_seen += 1
        by_command[cmd] = {
            "id": commands_seen,
            "invocation": invocation,
            "submitted": False,
            "completed": False,
        }
    emit(
        "compute-encoding-entered",
        submit=invocation[0],
        queue=invocation[1],
        command=by_command[cmd]["id"],
        workgroups=groups,
    )


def submitted(frame, thread):
    record = by_command.get(int(frame.read_var("commands")))
    if not record or record["invocation"] != calls.get(thread) or public_parent(frame) is None:
        return
    record["submitted"] = True
    # Entering a driver call is not evidence of acceptance or completion.
    emit("native-submission-entered", submit=record["invocation"][0], command=record["id"])


def completed(frame, _thread):
    commands = frame.read_var("commands")
    record = by_command.get(int(commands))
    if not record or not record["submitted"] or record["completed"]:
        return
    caller = frame.older()
    if (
        not caller
        or caller.name() not in ("doe_vk_commands_poll", "doe_vk_commands_wait")
        or not native_frame(caller)
    ):
        return
    state = str(commands.dereference()["state"])
    status = int(frame.read_var("status"))
    if state == "DOE_VK_SUBMITTED" and status == 0:
        # The specific Vulkan commands owner still describes an accepted
        # submission here. The poll/wait owner calls complete(OK) only after
        # VK_SUCCESS for its fence or a reached timeline semaphore value.
        record["completed"] = True
        emit(
            "native-fence-observed",
            submit=record["invocation"][0],
            queue=record["invocation"][1],
            command=record["id"],
            terminalSource=caller.name(),
        )
    elif status != 0:
        emit(
            "native-terminal-failure",
            submit=record["invocation"][0],
            command=record["id"],
            status=status,
        )


def released(frame, _thread):
    # Conservatively stop tracking at any release attempt (even BUSY). No
    # later reuse of an address can upgrade an ambiguous outcome to success.
    record = by_command.pop(int(frame.read_var("commands")), None)
    if record and not record["completed"]:
        retired_unresolved.add(record["id"])
        emit("release-without-observed-fence", submit=record["invocation"][0], command=record["id"])


def end(event):
    exit_code = getattr(event, "exit_code", None)
    unresolved = sorted(
        retired_unresolved | {r["id"] for r in by_command.values() if not r["completed"]}
    )
    emit(
        "collector-ended",
        exitCode=exit_code,
        unresolved=unresolved,
        errorCount=len(errors),
        errors=errors[:12],
        pageOwnership=None,
        latencyQualified=False,
    )
    log.close()


for fn, callback in (
    ("create_device", lambda frame, thread: queues.clear()),
    ("wgpuQueueSubmit", public_submit),
    ("doe_vk_commands_dispatch_resources", recorded),
    ("doe_vk_commands_submit", submitted),
    ("doe_vk_commands_release", released),
):
    Entry(fn, callback)
Entry("complete", completed, "/backends/vulkan/src/commands.c")
gdb.events.exited.connect(end)
emit(
    "collector-armed",
    pid=owner_pid,
    nativeLibrarySha256=expected,
    pageOwnership=None,
    latencyQualified=False,
)
