"""Offline tests using non-identifying frames observed on the owner's device."""
import unittest
from types import SimpleNamespace
from probe_batteryprotect_control import parse_frame, request, Session, DATA, LAST, OUTPUT

class ProtocolTests(unittest.TestCase):
    def test_instances(self):
        self.assertEqual(parse_frame(bytes.fromhex("029f0000ff")), [(2, [0, 0])])

    def test_mode_encoding(self):
        self.assertEqual(request(6, 0, [0x0200, b"\x04"]).hex(), "0600821902004104")
        self.assertEqual(request(6, 0, [0x0200, b"\x03"]).hex(), "0600821902004103")

    def test_off_confirmation(self):
        self.assertEqual(parse_frame(bytes.fromhex("080019eda8410008001902074404000000")),
                         [(8, 0, 0xeda8, b"\x00"), (8, 0, 0x0207, b"\x04\x00\x00\x00")])

    def test_output_transition_not_boolean(self):
        self.assertEqual(parse_frame(bytes.fromhex("080019eda84103")), [(8, 0, OUTPUT, b"\x03")])

    def test_unsupported_read(self):
        self.assertEqual(parse_frame(bytes.fromhex("090019edbb01")), [(9, 0, 0xedbb, 1)])

    def test_split_frame(self):
        report = {"notifications": [], "parse_errors": []}
        session = Session(None, report)
        session.notification(SimpleNamespace(uuid=DATA), bytes.fromhex("08001902074410000000080019020141f9080019"))
        self.assertEqual(session.values, {})
        session.notification(SimpleNamespace(uuid=LAST), bytes.fromhex("02004103"))
        self.assertEqual(session.values[0x0200], b"\x03")
        self.assertEqual(report["parse_errors"], [])

    def test_reject_unknown_and_truncated(self):
        for frame in ("0c", "0800190200", "080019020001"):
            with self.assertRaises(Exception):
                parse_frame(bytes.fromhex(frame))

if __name__ == "__main__":
    unittest.main()
