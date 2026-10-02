from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import trace_batches as batches


class TraceBatches(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        binary = Path(cls.temp.name) / 'fixture'
        subprocess.run(['g++', '-std=c++17', '-O2', '-DILLIXR_BATCH_TRACE=1', '-include', 'initializer_list',
                        '-I' + str(ROOT / 'src'), str(ROOT / 'tests/native/trace_batch_fixture.cpp'),
                        '-o', str(binary)], check=True, capture_output=True)
        cls.legacy = subprocess.check_output([str(binary)], text=True)
        cls.wire = subprocess.check_output([str(binary), 'batch'], text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_native_encoder_matches_native_printf_exactly_across_batches(self):
        text, stats = batches.decode_console(self.wire)
        self.assertEqual(text, self.legacy)
        self.assertGreater(stats['batches'], 1)
        self.assertEqual(stats['format_calls'], 4800)

    def test_saved_legacy_passes_through(self):
        self.assertEqual(batches.decode_console(self.legacy), (self.legacy, {'encoding': 'legacy'}))

    def test_missing_duplicate_reordered_corrupt_and_truncated_batches_rejected(self):
        lines = self.wire.splitlines(keepends=True)
        variations = [lines[1:], lines[:-1], lines[:1] + lines[2:],
                      lines[:2] + lines[1:], lines[:1] + [lines[2], lines[1]] + lines[3:],
                      lines[:-1] + [lines[-1].replace('ok', 'error')]]
        damaged = lines.copy(); fields = damaged[1].split(); fields[3] = '00000000'
        damaged[1] = ' '.join(fields) + '\n'; variations.append(damaged)
        for lines in variations:
            with self.subTest(lines=lines[:1]), self.assertRaises(ValueError):
                batches.decode_console(''.join(lines))

    def test_terminal_crlf_and_driver_diagnostics(self):
        text, _ = batches.decode_console('driver startup\n' + self.wire.replace('\n', '\r\n') + 'driver done\n')
        self.assertEqual(text, 'driver startup\n' + self.legacy + 'driver done\n')

    def test_unsupported_format_and_argument_mismatch_rejected(self):
        for fmt, args in [('%n', [('u', 42)]), ('%s', [('d', 1.0)]), ('%llu', []), ('x', [('i', 1)])]:
            with self.assertRaises(ValueError): batches.format_record(fmt, args)

    def test_original_wire_file_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'console.log'; p.write_text(self.wire)
            batches.decode_file(p)
            self.assertEqual(p.read_text(), self.legacy)
            self.assertEqual(p.with_name('console.batched.log').read_text(), self.wire)

    def test_raw_crlf_capture_is_preserved_byte_for_byte(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'console.log'
            original = self.wire.replace('\n', '\r\n').encode()
            p.write_bytes(original); batches.decode_file(p)
            self.assertEqual(p.with_name('console.batched.log').read_bytes(), original)
            self.assertEqual(p.read_text(), self.legacy)


if __name__ == '__main__': unittest.main()
