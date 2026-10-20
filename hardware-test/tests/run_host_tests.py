"""Compila el firmware real con perifericos simulados y ejecuta escenarios.

Uso: python tests/run_host_tests.py RUTA_A_ZIG
No simula tensiones, arbitraje electrico, interrupciones ni tiempos reales de UART.
"""
from pathlib import Path
import subprocess
import sys
import os

root = Path(__file__).resolve().parents[1]
out = root / '.host-test-build'
out.mkdir(exist_ok=True)
os.environ['ZIG_GLOBAL_CACHE_DIR'] = str(out / 'cache')
os.environ['ZIG_LOCAL_CACHE_DIR'] = str(out / 'local-cache')
compiler = [sys.argv[1], 'c++', '-std=c++17', '-O0', '-I', str(root/'tests/host')]
for board in (1, 2):
    subprocess.run(compiler + [f'-DDEVICE_ID={board}', '-c', str(root/'tests/host/board.cpp'),
                   '-o', str(out/f'board{board}.o')], check=True)
exe = out / 'scenarios.exe'
subprocess.run(compiler + [str(root/'tests/host/scenarios.cpp'), str(out/'board1.o'),
               str(out/'board2.o'), '-o', str(exe)], check=True)
for scenario in ('healthy', 'ttl2_only', 'can_only', 'rs485_only', 'all_down',
                 'one_way_ttl1', 'failover', 'corrupt_info', 'lost_reply', 'can_loss',
                 'restart', 'full_can_raw', 'mid_raw_failover', 'rs485_nuls'):
    subprocess.run([str(exe), scenario], check=True, timeout=60)
