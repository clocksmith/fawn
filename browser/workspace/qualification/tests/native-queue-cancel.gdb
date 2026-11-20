# Interrupt only the child launched by this CPU test at the first real wait for
# a submitted compute command, before the observer can see a terminal result.
python
class StopBeforeFence(gdb.Breakpoint):
    hit_tracked_command = False
    def stop(self):
        frame = gdb.newest_frame()
        if not native_frame(frame): return False
        entry = by_command.get(int(frame.read_var("commands")))
        if entry and entry["submitted"] and not entry["completed"] and str(frame.read_var("commands").dereference()["state"]) == "DOE_VK_SUBMITTED":
            self.hit_tracked_command = True
            return True
        return False
cancel = StopBeforeFence("doe_vk_command_wait_raw", internal=True)
end
continue
python
if not cancel.hit_tracked_command: raise gdb.GdbError("No accepted compute reached the pre-fence cancellation point")
end
kill
