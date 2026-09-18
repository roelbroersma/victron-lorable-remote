"""Reject an undersized socket configuration before a firmware image can build."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parent.parent
HEADER_DIR = ROOT / 'esp8684/main'


class SocketBudget(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which('gcc') or shutil.which('arm-none-eabi-gcc')
        if not cls.compiler:
            candidates = list((Path(os.environ.get('LOCALAPPDATA', '')) /
                'Arduino15/packages/rak_rui/tools/arm-none-eabi-gcc').glob('*/bin/arm-none-eabi-gcc.exe'))
            if candidates:
                cls.compiler = str(sorted(candidates)[-1])
        if not cls.compiler:
            raise RuntimeError('A C compiler is required; activate the firmware build environment')

    def preprocess(self, count):
        flags = [] if count is None else [f'-DCONFIG_LWIP_MAX_SOCKETS={count}']
        return subprocess.run([self.compiler, '-E', '-x', 'c', '-I', str(HEADER_DIR),
            *flags, '-'], input='#include "portal_socket_budget.h"\n',
            capture_output=True, text=True)

    def test_invalid_budget_is_compile_error(self):
        # 5 reproduces the portal startup failure; 8 fits HTTPD alone but not
        # USB's extra loopback client; 9 lacks the explicit service reserve.
        for count in (None, 5, 8, 9):
            with self.subTest(count=count):
                result = self.preprocess(count)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('error:', result.stderr)

    def test_valid_budget_builds(self):
        for count in (10, 16):
            result = self.preprocess(count)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_and_default_use_checked_budget(self):
        source = (HEADER_DIR / 'portal.c').read_text()
        self.assertIn('#include "portal_socket_budget.h"', source)
        self.assertEqual(re.findall(r'configuration\.max_open_sockets\s*=\s*(\w+)', source),
            ['LORABLE_HTTP_CLIENT_SOCKETS'])
        defaults = (ROOT / 'esp8684/sdkconfig.defaults').read_text()
        count = int(re.search(r'^CONFIG_LWIP_MAX_SOCKETS=(\d+)$', defaults, re.M)[1])
        result = self.preprocess(count)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_uart_reception_survives_flash_staging(self):
        defaults = (ROOT / 'esp8684/sdkconfig.defaults').read_text()
        self.assertRegex(defaults, r'(?m)^CONFIG_UART_ISR_IN_IRAM=y$')
        source = (HEADER_DIR / 'uart_link.c').read_text()
        self.assertIn('#if !CONFIG_UART_ISR_IN_IRAM', source)
        self.assertIn('#error "USB update staging requires', source)


if __name__ == '__main__':
    unittest.main()
