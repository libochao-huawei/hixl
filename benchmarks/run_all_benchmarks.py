"""Run all HIXL benchmarks (comm + KV) and generate perf.md.

Detects A2/A3/A5 (Ascend950) platforms, runs all valid communication and KV cache benchmarks,
generates perf.md directly from CSV outputs, and prints results to terminal.

Usage:
  python3 benchmarks/run_all_benchmarks.py
  python3 benchmarks/run_all_benchmarks.py --loops 10 --device_ids 0,1,2,3,4,5,6,7
  python3 benchmarks/run_all_benchmarks.py --skip-kv
  python3 benchmarks/run_all_benchmarks.py -H OPTION_KEY=OPTION_VALUE  # comm bench only (repeatable)
"""
from __future__ import annotations

import argparse
import logging
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

BENCHMARKS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BENCHMARKS_DIR.parent
if str(BENCHMARKS_DIR) not in sys.path:
    sys.path.insert(0, str(BENCHMARKS_DIR))

from benchmark_log import configure_logging  # noqa: E402
from kv_defaults import KV_PROCESS_COUNT_BY_PLATFORM, default_kv_transport, resolve_kv_devices  # noqa: E402
from platform_detect import detect_platform  # noqa: E402

configure_logging()
log = logging.getLogger(__name__)

RENDER_SCRIPT = BENCHMARKS_DIR / 'performance' / 'render_perf_md.py'
KV_BENCH_SCRIPT = BENCHMARKS_DIR / 'kv_benchmark' / 'scripts' / 'run_kv_benchmark.py'

MEM_TYPES = ['device', 'host']
OP_TYPES = ['write', 'read']
TRANSPORTS_A2 = ['hccs', 'rdma']
TRANSPORTS_A3 = ['hccs', 'rdma', 'fabric_mem']
TRANSPORTS_A5 = ['rdma', 'fabric_mem']
BLOCK_SIZES = [
    16 * 1024,
    32 * 1024,
    64 * 1024,
    128 * 1024,
    256 * 1024,
    512 * 1024,
    1 * 1024 * 1024,
    2 * 1024 * 1024,
]
KV_MODELS = ['deepseek-r1', 'glm5', 'deepseek-v4']


@dataclass
class CommRunSpec:
    bench_bin: str
    role: str
    transport: str
    initiator_mem: str
    target_mem: str
    op_type: str
    loops: int
    output_dir: Path
    hixl_port: int
    tcp_port: int
    device_id: int
    platform_id: str
    target_host: str = '127.0.0.1'
    hixl_init_options: list[str] | None = None


def find_binary(name: str, *rel_paths: str) -> str:
    for rel in rel_paths:
        p = PROJECT_ROOT / 'build' / 'benchmarks' / rel
        if p.exists():
            return str(p)
    return f'./{name}'


def _make_cmd(spec: CommRunSpec) -> list[str]:
    """Build command line for target or initiator."""
    start_bs = BLOCK_SIZES[0]
    max_bs = BLOCK_SIZES[-1]
    base_args = [
        f'--transport={spec.transport}',
        f'--initiator_memory={spec.initiator_mem}',
        f'--target_memory={spec.target_mem}',
        f'--op_type={spec.op_type}',
        f'--start_block_size={start_bs}',
        f'--max_block_size={max_bs}',
        '--start_threads=1',
        '--max_threads=1',
        f'--loops={spec.loops}',
        f'--output_dir={spec.output_dir}',
        f'--soc_variant={spec.platform_id}',
    ]
    opt_args: list[str] = []
    if spec.hixl_init_options:
        for payload in spec.hixl_init_options:
            opt_args.append(f'-H={payload}')
    if spec.role == 'target':
        return [
            spec.bench_bin,
            '--role=target',
            f'--device_id={spec.device_id}',
            f'--local_engine={spec.target_host}:{spec.hixl_port}',
            f'--tcp_port={spec.tcp_port}',
            *base_args,
            *opt_args,
        ]
    remote = spec.target_host if spec.target_host else '127.0.0.1'
    return [
        spec.bench_bin,
        '--role=initiator',
        f'--device_id={spec.device_id}',
        f'--local_engine=127.0.0.1:{spec.hixl_port + 1}',
        f'--remote_engine={remote}:{spec.hixl_port}',
        f'--tcp_port={spec.tcp_port}',
        *base_args,
        *opt_args,
    ]


def _wait_for_combo_processes(server: subprocess.Popen, client_cmd: list[str]) -> tuple[int, int] | None:
    try:
        client = subprocess.Popen(client_cmd)
        client_rc = client.wait(timeout=300)
        server_rc = server.wait(timeout=10)
        return server_rc, client_rc
    except subprocess.TimeoutExpired:
        log.error('[FAIL] Timeout — killing processes')
        server.kill()
        server.wait()
        return None


def run_combo(
    bench_bin: str,
    transport: str,
    initiator_mem: str,
    target_mem: str,
    op_type: str,
    loops: int,
    output_dir: Path,
    hixl_port: int,
    tcp_port: int,
    device_id0: int,
    device_id1: int,
    platform_id: str,
    target_host: str = '127.0.0.1',
    hixl_init_options: list[str] | None = None,
) -> bool:
    """Run one target+initiator pair. Returns True on success."""
    output_dir.mkdir(parents=True, exist_ok=True)
    common = dict(
        bench_bin=bench_bin,
        transport=transport,
        initiator_mem=initiator_mem,
        target_mem=target_mem,
        op_type=op_type,
        loops=loops,
        output_dir=output_dir,
        hixl_port=hixl_port,
        tcp_port=tcp_port,
        platform_id=platform_id,
        target_host=target_host,
        hixl_init_options=hixl_init_options,
    )
    server_cmd = _make_cmd(CommRunSpec(role='target', device_id=device_id1, **common))
    client_cmd = _make_cmd(CommRunSpec(role='initiator', device_id=device_id0, **common))
    direction = _compute_direction(initiator_mem, target_mem, op_type)
    log.info(f"\n{'=' * 60}")
    log.info(f'[RUN] {direction} transport={transport} initiator={initiator_mem} target={target_mem}')
    log.info(f"[CMD] server: {' '.join(server_cmd)}")
    log.info(f"[CMD] client: {' '.join(client_cmd)}")
    server = subprocess.Popen(server_cmd)
    time.sleep(2)
    rc_pair = _wait_for_combo_processes(server, client_cmd)
    if rc_pair is None:
        return False
    server_rc, client_rc = rc_pair
    ok = server_rc == 0 and client_rc == 0
    label = '[OK]' if ok else f'[FAIL] server_rc={server_rc} client_rc={client_rc}'
    log.info(f'{label}   {direction} {transport}')
    return ok


def run_kv_benchmarks(
    kv_bin: str,
    devices: list[int],
    output_dir: Path,
    models: list[str],
    transport: str,
    platform_id: str,
) -> int:
    ok = 0
    device_csv = ','.join(str(d) for d in devices)
    for model in models:
        log.info(f"\n{'=' * 60}")
        log.info(f'[RUN] KV benchmark model={model} transport={transport}')
        cmd = [
            sys.executable,
            str(KV_BENCH_SCRIPT),
            f'--bench_bin={kv_bin}',
            f'--model={model}',
            f'--transport={transport}',
            f'--platform={platform_id}',
            f'--num_processes={len(devices)}',
            f'--devices={device_csv}',
            f'--output_dir={output_dir}',
        ]
        log.info(f"[CMD] {' '.join(cmd)}")
        ret = subprocess.run(cmd).returncode
        if ret == 0:
            log.info(f'[OK]   KV {model}')
            ok += 1
        else:
            log.error(f'[FAIL] KV {model} returncode={ret}')
        time.sleep(1)
    return ok


def _compute_direction(im: str, tm: str, op: str) -> str:
    wr = op == 'write'
    if im == 'device' and tm == 'device':
        return 'D2rD' if wr else 'rD2D'
    if im == 'device' and tm == 'host':
        return 'D2rH' if wr else 'rH2D'
    if im == 'host' and tm == 'host':
        return 'H2rH' if wr else 'rH2H'
    if im == 'host' and tm == 'device':
        return 'H2rD' if wr else 'rD2H'
    return 'unknown'


def _hccs_supports_combo(platform_id: str, initiator_mem: str, target_mem: str) -> bool:
    if platform_id == 'a5':
        return False
    if initiator_mem == 'device' and target_mem == 'device':
        return True
    return platform_id == 'a3' and initiator_mem == 'host' and target_mem == 'device'


def _transports_for_platform(platform_id: str) -> list[str]:
    if platform_id == 'a3':
        return TRANSPORTS_A3
    if platform_id == 'a5':
        return TRANSPORTS_A5
    return TRANSPORTS_A2


def parse_all_benchmark_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description='Run all HIXL benchmarks')
    parser.add_argument('--loops', type=int, default=5, help='Repeat full block-step ladder per comm combo (hixl_comm_bench --loops).')
    parser.add_argument('--device_ids', type=str, default='0,1,2,3,4,5,6,7', help='Device IDs (default: 0-7). First 2 used for comm; KV uses all on A2/A5, or 0-15 on A3 when fewer than 16 IDs are given.')
    parser.add_argument('--platform', type=str, default=None, choices=['a2', 'a3', 'a5'], help='Skip npu-smi detection and use this platform (a2/a3/a5).')
    parser.add_argument('--base_hixl_port', type=int, default=17000)
    parser.add_argument('--base_tcp_port', type=int, default=21000)
    parser.add_argument('--target_host', type=str, default='127.0.0.1', help='Deprecated. Use run_comm_benchmark.py --role for dual-machine mode.')
    parser.add_argument('--skip_comm', action='store_true', help='Skip communication benchmarks')
    parser.add_argument('--skip_kv', action='store_true', help='Skip KV benchmarks')
    parser.add_argument('-H', '--hixl_option', action='append', default=[], metavar='KEY=VALUE', help='HIXL Initialize() option for hixl_comm_bench only, same as its -H=KEY=VALUE (repeatable). Ignored when --skip_comm.')
    return parser.parse_args()


def _validate_args(args: argparse.Namespace, devs: list[int]) -> int | None:
    if len(devs) < 2:
        log.error('[ERROR] Need at least 2 device IDs')
        return 1
    if args.target_host != '127.0.0.1':
        log.error('[ERROR] run_all_benchmarks.py does not support true dual-machine orchestration.')
        log.error('[ERROR] Use benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --role=target/initiator instead.')
        return 1
    return None


def run_comm_suite(args: argparse.Namespace, platform_id: str, devs: list[int]) -> tuple[int, int]:
    comm_bin = find_binary('hixl_comm_bench', 'comm_benchmark/hixl_comm_bench')
    if not Path(comm_bin).exists():
        log.error(f'[ERROR] comm bench binary not found: {comm_bin}')
        log.error('[ERROR] Build with: bash build.sh --examples')
        raise RuntimeError('comm bench binary missing')
    comm_output = BENCHMARKS_DIR / 'comm_benchmark' / 'output'
    for old_csv in comm_output.glob('comm_result_*.csv'):
        old_csv.unlink()
    transports = _transports_for_platform(platform_id)
    comm_ok = 0
    comm_total = 0
    port_offset = 0
    hixl_opts = args.hixl_option or None
    for transport in transports:
        for im in MEM_TYPES:
            for tm in MEM_TYPES:
                for op in OP_TYPES:
                    direction = _compute_direction(im, tm, op)
                    if transport == 'hccs' and not _hccs_supports_combo(platform_id, im, tm):
                        log.info(
                            f'[SKIP] {direction} transport=hccs not supported on this platform '
                            '(A2: HCCS D2D only; A3: add H2rD|rD2H; A5/Ascend950: no HCCS)'
                        )
                        continue
                    comm_total += 1
                    hixl_port = args.base_hixl_port + port_offset * 2
                    tcp_port = args.base_tcp_port + port_offset
                    port_offset += 1
                    if run_combo(
                        comm_bin,
                        transport,
                        im,
                        tm,
                        op,
                        args.loops,
                        comm_output,
                        hixl_port,
                        tcp_port,
                        devs[0],
                        devs[1],
                        platform_id,
                        args.target_host,
                        hixl_opts,
                    ):
                        comm_ok += 1
                    time.sleep(1)
    log.info(f'\n[INFO] Comm: {comm_ok}/{comm_total} runs succeeded')
    return comm_ok, comm_total


def render_perf_report(platform_id: str, comm_ok: int) -> None:
    comm_csv_dir = BENCHMARKS_DIR / 'comm_benchmark' / 'output'
    if comm_ok <= 0 or not list(comm_csv_dir.glob('comm_result_*.csv')):
        return
    log.info('\n[INFO] Generating perf.md from CSVs...')
    render_cmd = [
        sys.executable,
        str(RENDER_SCRIPT),
        f'--csv-dir={comm_csv_dir}',
        f'--platform={platform_id}',
    ]
    ret = subprocess.run(render_cmd, check=False)
    if ret.returncode != 0:
        log.warning('[WARN] render_perf_md.py failed')


def main() -> None:
    args = parse_all_benchmark_args()
    devs = [int(x.strip()) for x in args.device_ids.split(',')]
    err = _validate_args(args, devs)
    if err is not None:
        sys.exit(err)
    platform_id = detect_platform(args.platform)
    log.info(f'[INFO] Platform: {platform_id}, transports: {_transports_for_platform(platform_id)}')
    comm_ok = 0
    if not args.skip_comm:
        comm_ok, _ = run_comm_suite(args, platform_id, devs)
    kv_ok = 0
    if not args.skip_kv:
        kv_bin = find_binary('hixl_kv_bench', 'kv_benchmark/hixl_kv_bench')
        if not Path(kv_bin).exists():
            log.warning(f'[WARN] KV bench binary not found: {kv_bin}, skipping KV benchmarks')
        else:
            kv_output = BENCHMARKS_DIR / 'kv_benchmark' / 'output'
            kv_transport = default_kv_transport(platform_id)
            log.info(f'[INFO] KV benchmark transport={kv_transport} (platform={platform_id})')
            kv_process_count = len(devs) if devs else KV_PROCESS_COUNT_BY_PLATFORM.get(platform_id, 8)
            kv_devs = resolve_kv_devices(platform_id, devs, kv_process_count)
            log.info(f"[INFO] KV benchmark devices ({len(kv_devs)}): {','.join(str(d) for d in kv_devs)}")
            kv_ok = run_kv_benchmarks(kv_bin, kv_devs, kv_output, KV_MODELS, kv_transport, platform_id)
            log.info(f'\n[INFO] KV: {kv_ok}/{len(KV_MODELS)} runs succeeded')
    if comm_ok + kv_ok == 0:
        log.warning('[WARN] No successful benchmarks, skipping perf.md generation')
        return
    render_perf_report(platform_id, comm_ok)
    log.info('\n[DONE]')


if __name__ == '__main__':
    try:
        main()
    except RuntimeError as exc:
        log.error('%s', exc)
        sys.exit(1)
