import sys
import unittest
from pathlib import Path


SCRIPTS_DIR = Path(__file__).resolve().parents[2] / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

import firmware_budget_report  # noqa: E402
import firmware_size_history  # noqa: E402
import pre_release_check  # noqa: E402


BUILD_LOG_WITH_INTERMEDIATE_IMAGE = """
RAM:   [=         ]  11.4% (used 37276 bytes from 327680 bytes)
Flash: [=         ]  14.9% (used 978322 bytes from 6553600 bytes)
RAM:   [==        ]  17.8% (used 58212 bytes from 327680 bytes)
Flash: [==========]  97.3% (used 6375927 bytes from 6553600 bytes)
"""


class FirmwareBudgetReportTest(unittest.TestCase):
    def test_budget_report_uses_final_platformio_size(self):
        self.assertEqual(
            firmware_budget_report.parse_size(
                firmware_budget_report.FLASH_RE,
                BUILD_LOG_WITH_INTERMEDIATE_IMAGE,
                "flash",
            ),
            (6375927, 6553600),
        )
        self.assertEqual(
            firmware_budget_report.parse_size(
                firmware_budget_report.RAM_RE,
                BUILD_LOG_WITH_INTERMEDIATE_IMAGE,
                "RAM",
            ),
            (58212, 327680),
        )

    def test_release_gate_uses_final_platformio_size(self):
        self.assertEqual(
            pre_release_check.parse_size(
                pre_release_check.FLASH_RE,
                BUILD_LOG_WITH_INTERMEDIATE_IMAGE,
                "flash",
            ),
            (6375927, 6553600),
        )

    def test_size_history_uses_final_platformio_size(self):
        self.assertEqual(
            firmware_size_history.parse_size_line(
                firmware_size_history.FLASH_RE,
                BUILD_LOG_WITH_INTERMEDIATE_IMAGE,
            ),
            6375927,
        )


if __name__ == "__main__":
    unittest.main()
