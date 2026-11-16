import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("persistent", Path(__file__).resolve().parents[2] / "scripts/ci/PersistentBuild.py")
cache = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cache)


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.meta = self.root / ".git/cache.json"

    def write(self, relative, content="data"):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def test_warm_run_retains_objects_and_relinks_outputs(self):
        cache.prepare(self.root, "linux", "Release", "compiler-a", self.meta)
        obj = self.write("bin-int/Release-linux-x86_64/Core/object.o")
        final = self.write("bin/Release-linux-x86_64/Core/libCore.a")
        source = self.write("Core/Source/current.cpp")
        stale_log = self.write("logs/previous.log")
        self.assertTrue(cache.prepare(self.root, "linux", "Release", "compiler-a", self.meta))
        self.assertTrue(obj.exists())
        self.assertTrue(source.exists())
        self.assertFalse(final.exists())
        self.assertFalse(stale_log.exists())

    def test_toolchain_change_clears_only_generated_data(self):
        cache.prepare(self.root, "windows", "Debug", "compiler-a", self.meta)
        obj = self.write("bin-int/Debug-windows-x86_64/Core/object.obj")
        source = self.write("Core/Source/current.cpp")
        self.assertFalse(cache.prepare(self.root, "windows", "Debug", "compiler-b", self.meta))
        self.assertFalse(obj.exists())
        self.assertTrue(source.exists())

    def test_other_configuration_is_retained(self):
        cache.prepare(self.root, "windows", "Debug", "same-tools", self.meta)
        release = self.write("bin/Release-windows-x86_64/Core/Core.lib")
        debug = self.write("bin/Debug-windows-x86_64/Core/Core.lib")
        cache.prepare(self.root, "windows", "Debug", "same-tools", self.meta)
        self.assertTrue(release.exists())
        self.assertFalse(debug.exists())

    def test_cleanup_refuses_traversal_and_repo_root(self):
        for relative in ("../outside", "."):
            with self.assertRaises(RuntimeError):
                cache.remove_generated(self.root, relative)


if __name__ == "__main__":
    unittest.main()
