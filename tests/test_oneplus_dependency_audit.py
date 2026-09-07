import importlib.util
import pathlib
import sys
import unittest

FIRMWARE = pathlib.Path(__file__).resolve().parents[1] / "tools/firmware"
sys.path.insert(0, str(FIRMWARE))
SPEC = importlib.util.spec_from_file_location("oneplus_audit", FIRMWARE / "audit_oneplus_shim.py")
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
sys.path.pop(0)


class DependencyAuditTest(unittest.TestCase):
    def test_aarch64_ifunc_and_symbol_versions(self):
        tables = MODULE.parse_elf_tables('''
 0x0000000000000001 (NEEDED) Shared library: [libc.so]
 391: 0000000000108fe8 36 <OS specific>: 10 GLOBAL DEFAULT 15 memcpy@@LIBC
 392: 0000000000108fe8 36 IFUNC GLOBAL DEFAULT 15 memchr@@LIBC
 400: 0000000000108fe8 36 FUNC GLOBAL DEFAULT 15 legacy@LIBC_OLD
 401: 0000000000108fe8 36 FUNC GLOBAL HIDDEN 15 hidden
 402: 0000000000108fe8 36 FUNC LOCAL DEFAULT 15 local
 403: 0000000000000000 0 FUNC GLOBAL DEFAULT UND memcpy@LIBC (2)
 404: 0000000000000000 0 NOTYPE WEAK DEFAULT UND optional
''')
        self.assertEqual(["libc.so"], tables["needed"])
        self.assertEqual({"memcpy", "memcpy@LIBC", "memchr", "memchr@LIBC", "legacy@LIBC_OLD"}, tables["exports"])
        self.assertEqual({"memcpy@LIBC"}, tables["imports"])
        self.assertEqual({"optional"}, tables["weak_imports"])


if __name__ == "__main__":
    unittest.main()
