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
import subprocess
import sys
import time
from pathlib import Path
import logging
PROJECT_ROOT = Path(__file__).resolve().parents[1]
BENCHMARKS_DIR = Path(__file__).resolve().parent
if str(BENCHMARKS_DIR) not in sys.path:
    sys.path.insert(0, str(BENCHMARKS_DIR))
RENDER_SCRIPT = BENCHMARKS_DIR / 'performance' / 'render_perf_md.py'
KV_BENCH_SCRIPT = BENCHMARKS_DIR / 'kv_benchmark' / 'scripts' / 'run_kv_benchmark.py'
from kv_defaults import KV_PROCESS_COUNT_BY_PLATFORM, default_kv_transport, resolve_kv_devices
from platform_detect import detect_platform
from benchmark_log import configure_logging
configure_logging()
log = logging.getLogger(__name__)
MEM_TYPES = ['device', 'host']
OP_TYPES = ['write', 'read']
TRANSPORTS_A2 = ['hccs', 'rdma']
TRANSPORTS_A3 = ['hccs', 'rdma', 'fabric_mem']
TRANSPORTS_A5 = ['rdma', 'fabric_mem']
BLOCK_SIZES = [16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024, 256 * 1024, 512 * 1024, 1 * 1024 * 1024, 2 * 1024 * 1024]
KV_MODELS = ['deepseek-r1', 'glm5', 'deepseek-v4']

def find_binary(name: str, *rel_paths: str) -> str:
    for rel in rel_paths:
        p = PROJECT_ROOT / 'build' / 'benchmarks' / rel
        if p.exists():
            return str(p)
    return f'./{name}'

def _make_cmd(bench_bin: str, role: str, transport: str, initiator_mem: str, target_mem: str, op_type: str, loops: int, output_dir: Path, hixl_port: int, tcp_port: int, device_id: int, platform_id: str, target_host: str='127.0.0.1', hixl_init_options: list[str] | None=None) -> list[str]:
    """Build command line for target or initiator."""
    start_bs = BLOCK_SIZES[0]
    max_bs = BLOCK_SIZES[-1]
    base_args = [f'--transport={transport}', f'--initiator_memory={initiator_mem}', f'--target_memory={target_mem}', f'--op_type={op_type}', f'--start_block_size={start_bs}', f'--max_block_size={max_bs}', f'--start_threads=1', f'--max_threads=1', f'--loops={loops}', f'--output_dir={output_dir}', f'--soc_variant={platform_id}']
    opt_args: list[str] = []
    if hixl_init_options:
        for payload in hixl_init_options:
            opt_args.append(f'-H={payload}')
    if role == 'target':
        return [bench_bin, '--role=target', f'--device_id={device_id}', f'--local_engine={target_host}:{hixl_port}', f'--tcp_port={tcp_port}', *base_args, *opt_args]
    else:
        remote = target_host if target_host else '127.0.0.1'
        return [bench_bin, '--role=initiator', f'--device_id={device_id}', f'--local_engine=127.0.0.1:{hixl_port + 1}', f'--remote_engine={remote}:{hixl_port}', f'--tcp_port={tcp_port}', *base_args, *opt_args]

def run_combo(bench_bin: str, transport: str, initiator_mem: str, target_mem: str, op_type: str, loops: int, output_dir: Path, hixl_port: int, tcp_port: int, device_id0: int, device_id1: int, platform_id: str, target_host: str='127.0.0.1', hixl_init_options: list[str] | None=None) -> bool:
    """Run one target+initiator pair. Returns True on success."""
    output_dir.mkdir(parents=True, exist_ok=True)
    server_cmd = _make_cmd(bench_bin, 'target', transport, initiator_mem, target_mem, op_type, loops, output_dir, hixl_port, tcp_port, device_id1, platform_id, target_host=target_host, hixl_init_options=hixl_init_options)
    client_cmd = _make_cmd(bench_bin, 'initiator', transport, initiator_mem, target_mem, op_type, loops, output_dir, hixl_port, tcp_port, device_id0, platform_id, target_host, hixl_init_options=hixl_init_options)
    direction = _compute_direction(initiator_mem, target_mem, op_type)
    log.info(f"\n{'=' * 60}")
    log.info(f'[RUN] {direction} transport={transport} initiator={initiator_mem} target={target_mem}')
    log.info(f"[CMD] server: {' '.join(server_cmd)}")
    log.info(f"[CMD] client: {' '.join(client_cmd)}")
    server = subprocess.Popen(server_cmd)
    time.sleep(2)
    try:
        client = subprocess.Popen(client_cmd)
        client_rc = client.wait(timeout=300)
        server_rc = server.wait(timeout=10)
    except subprocess.TimeoutExpired:
        log.error('[FAIL] Timeout — killing processes')
        server.kill()
        server.wait()
        return False
    ok = server_rc == 0 and client_rc == 0
    label = '[OK]' if ok else f'[FAIL] server_rc={server_rc} client_rc={client_rc}'
    log.info(f'{label}   {direction} {transport}')
    return ok

def run_kv_benchmarks(kv_bin: str, devices: list[int], output_dir: Path, models: list[str], transport: str, platform_id: str) -> int:
    ok = 0
    for model in models:
        log.info(f"\n{'=' * 60}")
        log.info(f'[RUN] KV benchmark model={model} transport={transport}')
        cmd = [sys.executable, str(KV_BENCH_SCRIPT), f'--bench_bin={kv_bin}', f'--model={model}', f'--transport={transport}', f'--platform={platform_id}', f'--num_processes={len(devices)}', f"--devices={','.join((str(d) for d in devices))}", f'--output_dir={output_dir}']
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
    return platform_id == 'a3' and initiator_mem == 'host' and (target_mem == 'device')

def main() -> None:
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
    args = parser.parse_args()
    devs = [int(x.strip()) for x in args.device_ids.split(',')]
    if len(devs) < 2:
        log.error('[ERROR] Need at least 2 device IDs')
        sys.exit(1)
    if args.target_host != '127.0.0.1':
        log.error('[ERROR] run_all_benchmarks.py does not support true dual-machine orchestration.')
        log.error('[ERROR] Use benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --role=target/initiator instead.')
        sys.exit(1)
    platform_id = detect_platform(args.platform)
    if platform_id == 'a3':
        transports = TRANSPORTS_A3
    elif platform_id == 'a5':
        transports = TRANSPORTS_A5
    else:
        transports = TRANSPORTS_A2
    log.info(f'[INFO] Platform: {platform_id}, transports: {transports}')
    comm_ok = 0
    comm_total = 0
    if not args.skip_comm:
        comm_bin = find_binary('hixl_comm_bench', 'comm_benchmark/hixl_comm_bench')
        if not Path(comm_bin).exists():
            log.error(f'[ERROR] comm bench binary not found: {comm_bin}')
            log.error('[ERROR] Build with: bash build.sh --examples')
            sys.exit(1)
        comm_output = BENCHMARKS_DIR / 'comm_benchmark' / 'output'
        for old_csv in comm_output.glob('comm_result_*.csv'):
            old_csv.unlink()
        port_offset = 0
        for transport in transports:
            for im in MEM_TYPES:
                for tm in MEM_TYPES:
                    for op in OP_TYPES:
                        direction = _compute_direction(im, tm, op)
                        if transport == 'hccs' and (not _hccs_supports_combo(platform_id, im, tm)):
                            log.info(f'[SKIP] {direction} transport=hccs not supported on this platform (A2: HCCS D2D only; A3: add H2rD|rD2H; A5/Ascend950: no HCCS)')
                            continue
                        comm_total += 1
                        hixl_port = args.base_hixl_port + port_offset * 2
                        tcp_port = args.base_tcp_port + port_offset
                        port_offset += 1
                        if run_combo(comm_bin, transport, im, tm, op, args.loops, comm_output, hixl_port, tcp_port, devs[0], devs[1], platform_id, args.target_host, args.hixl_option or None):
                            comm_ok += 1
                        time.sleep(1)
        log.info(f'\n[INFO] Comm: {comm_ok}/{comm_total} runs succeeded')
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
            log.info(f"[INFO] KV benchmark devices ({len(kv_devs)}): {','.join((str(d) for d in kv_devs))}")
            kv_ok = run_kv_benchmarks(kv_bin, kv_devs, kv_output, KV_MODELS, kv_transport, platform_id)
            log.info(f'\n[INFO] KV: {kv_ok}/{len(KV_MODELS)} runs succeeded')
    total_ok = comm_ok + kv_ok
    if total_ok == 0:
        log.warning('[WARN] No successful benchmarks, skipping perf.md generation')
        return
    comm_csv_dir = BENCHMARKS_DIR / 'comm_benchmark' / 'output'
    if comm_ok > 0 and list(comm_csv_dir.glob('comm_result_*.csv')):
        log.info('\n[INFO] Generating perf.md from CSVs...')
        render_cmd = [sys.executable, str(RENDER_SCRIPT), f'--csv-dir={comm_csv_dir}', f'--platform={platform_id}']
        ret = subprocess.run(render_cmd, check=False)
        if ret.returncode != 0:
            log.warning('[WARN] render_perf_md.py failed')
    log.info('\n[DONE]')
if __name__ == '__main__':
    main()
