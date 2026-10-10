"""Original-source payload binding cannot be replaced by a diagnostic input."""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_postprocess_hle_reference import require_original_payload


class OriginalPayload(unittest.TestCase):
    def test_exact_source_bytes_admitted_and_metadata_retained(self):
        with TemporaryDirectory() as directory:
            path=Path(directory)/'input.bin';path.write_bytes(b'original supplied inputs')
            with patch('native_postprocess_hle_reference.payload',return_value=(path.read_bytes(),{'source':'authored'})):
                self.assertEqual(require_original_payload(SimpleNamespace(native_input=path)),{'source':'authored'})

    def test_changed_bias_or_state_and_truncated_payload_reject(self):
        original=b'original supplied inputs'
        for observed in (original[:-1],b'changed supplied inputs',original+b'extra'):
            with self.subTest(observed=observed),TemporaryDirectory() as directory:
                path=Path(directory)/'input.bin';path.write_bytes(observed)
                with patch('native_postprocess_hle_reference.payload',return_value=(original,{})):
                    with self.assertRaises(ValueError):
                        require_original_payload(SimpleNamespace(native_input=path))


if __name__=='__main__':unittest.main()
