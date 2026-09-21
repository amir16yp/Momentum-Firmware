"""Verify resource progress coalescing without suppressing percentage changes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ExtractionProgressTest(unittest.TestCase):
    def test_progress_updates(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "applications/system/updater/util/update_task_worker_backup.c").read_text()
        callback = source[source.index("static void update_task_resource_unpack_cb("):
                          source.index("static void update_task_cleanup_resources(")]
        harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
enum { UpdateTaskStageProgress };
typedef struct { struct { uint8_t stage_progress; } state; } UpdateTask;
static unsigned updates;
static void update_task_set_progress(UpdateTask* task, int stage, uint8_t percent) {
    assert(stage == UpdateTaskStageProgress);
    assert(percent != task->state.stage_progress);
    task->state.stage_progress = percent;
    ++updates;
}
'''
        harness += callback
        harness += r'''
int main(void) {
    UpdateTask task = {0};
    for(size_t byte = 0; byte <= 100000; ++byte) {
        update_task_resource_unpack_cb(byte, 100000, &task);
        assert(task.state.stage_progress == byte * 100 / 100001);
    }
    assert(updates == 99);
    // Repeated notifications at the same position do nothing.
    for(unsigned i = 0; i < 100; ++i) update_task_resource_unpack_cb(100000, 100000, &task);
    assert(updates == 99);
    // A new/empty stream may reset the displayed percentage.
    update_task_resource_unpack_cb(0, 0, &task);
    assert(task.state.stage_progress == 0 && updates == 100);
    update_task_resource_unpack_cb(0, 0, &task);
    assert(updates == 100);
    return 0;
}
'''
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "progress.c"
            binary = Path(directory) / "progress.exe"
            test.write_text(harness)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(test), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
