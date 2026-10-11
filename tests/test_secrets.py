import importlib.util
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_secrets", ROOT / "scripts" / "gen_secrets.py")
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class SecretBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        nonce = uuid.uuid4().hex
        self.values = {
            "WIFI_SSID": f"ssid-{nonce}\"\\x",
            "WIFI_PASSWORD": f"pass-{nonce}\n\\7",
            "OTA_PASSWORD": f"ota-{nonce}é",
        }

    def test_header_is_private_and_cpp_bytes_round_trip(self):
        target = self.root / ".pio-local" / "build_secrets.hpp"
        GENERATOR.write_header(target, self.values)
        self.assertTrue(target.is_file())
        self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)
        content = target.read_text()
        for value in self.values.values():
            self.assertNotIn(value, content)
        self.assertIn("#pragma once", content)
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler)
        source = self.root / "probe.cpp"
        source.write_text(
            '#include "build_secrets.hpp"\n'
            '#include <cstdio>\n'
            'int main() {\n'
            '  for (unsigned char c : BuildSecrets::wifi_ssid) if (c) std::printf("%02X", c);\n'
            '  std::puts("");\n'
            '  for (unsigned char c : BuildSecrets::wifi_password) if (c) std::printf("%02X", c);\n'
            '  std::puts("");\n'
            '  for (unsigned char c : BuildSecrets::ota_password) if (c) std::printf("%02X", c);\n'
            '  std::puts("");\n'
            '}\n'
        )
        binary = self.root / "probe"
        subprocess.run([compiler, "-std=c++11", "-I", str(target.parent),
                        str(source), "-o", str(binary)], check=True, capture_output=True)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
        self.assertEqual(result.stdout.splitlines(),
                         [self.values[key].encode().hex().upper() for key in self.values])

    def test_missing_input_does_not_replace_existing_header(self):
        target = self.root / ".pio-local" / "build_secrets.hpp"
        GENERATOR.write_header(target, self.values)
        original = target.read_bytes()
        missing = dict(self.values)
        del missing["OTA_PASSWORD"]
        with self.assertRaises(ValueError):
            GENERATOR.write_header(target, missing)
        self.assertEqual(target.read_bytes(), original)

    def test_wrapper_keeps_values_out_of_command_args_and_tracked_files(self):
        scripts = self.root / "scripts"
        scripts.mkdir()
        for name in ("build.sh", "pio.sh", "gen_secrets.py"):
            shutil.copy2(ROOT / "scripts" / name, scripts / name)
        shutil.copy2(ROOT / ".env.tpl", self.root / ".env.tpl")
        tools = self.root / "fake-bin"
        tools.mkdir()
        op_args = self.root / "op-args"
        pio_args = self.root / "pio-args"
        pio_env = self.root / "pio-env"
        (tools / "op").write_text(
            '#!/bin/sh\nprintf "%s\\n" "$@" > "$OP_ARGS"\n'
            'while [ "$1" != "--" ]; do shift; done\nshift\nexec "$@"\n'
        )
        (tools / "uvx").write_text(
            '#!/bin/sh\nprintf "%s\\n" "$@" > "$PIO_ARGS"\n'
            ': > "$PIO_ENV"\n'
            'if [ "${WIFI_SSID+x}" = x ] || [ "${WIFI_PASSWORD+x}" = x ] || '
            '[ "${OTA_PASSWORD+x}" = x ]; then printf "present\\n" >> "$PIO_ENV"; fi\n'
        )
        for name in ("op", "uvx"):
            (tools / name).chmod(0o755)
        env = os.environ.copy()
        env.pop("OP_SERVICE_ACCOUNT_TOKEN", None)
        env.update(self.values)
        env.update({"PATH": f"{tools}:{env['PATH']}", "OP_ARGS": str(op_args),
                    "PIO_ARGS": str(pio_args), "PIO_ENV": str(pio_env)})
        result = subprocess.run([str(scripts / "build.sh")], cwd=self.root,
                                env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / ".pio-local" / "build_secrets.hpp").is_file())
        self.assertEqual(op_args.read_text().splitlines()[:3],
                         ["run", "--env-file=.env.tpl", "--"])
        self.assertEqual(pio_args.read_text().splitlines(),
                         ["--from", "platformio==6.2.0", "pio", "run", "-e", "cores3"])
        for value in self.values.values():
            self.assertNotIn(value, op_args.read_text() + pio_args.read_text() + pio_env.read_text())
        self.assertEqual(pio_env.read_text(), "")
        ignored = subprocess.run(["git", "check-ignore", "-q", ".pio-local/build_secrets.hpp"],
                                 cwd=ROOT)
        self.assertEqual(ignored.returncode, 0)


if __name__ == "__main__":
    unittest.main()
