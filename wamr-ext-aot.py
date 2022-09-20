#!/usr/bin/env python3
import argparse
import subprocess
import sys

AOT_TARGETS = [
    'arm-gnu',
    'arm-gnueabi',
    'arm-gnueabihf',
    'aarch64-gnu',
    'x86_64-gnu'
]

if __name__ == '__main__':
    arg_parser = argparse.ArgumentParser(description='A tool to compile WASM binary to AOT binary for wamr-ext')
    arg_parser.add_argument('TARGET', type=str, choices=AOT_TARGETS, help='AOT target')
    arg_parser.add_argument('INPUT_FILE', type=str, help='Input WASM binary file')
    arg_parser.add_argument('-o', type=str, help='Output file', default='a.aot')

    argv = vars(arg_parser.parse_args())
    aot_target = argv['TARGET']
    aot_arch_target = aot_target.split('-')[0]
    aot_abi = aot_target.split('-')[-1]
    if aot_abi == 'gnueabi':
        aot_abi = 'eabi'
    aot_cpu = 'generic'
    aot_cpu_features = ''
    if aot_target.startswith('arm'):
        aot_arch_target = 'armv6'
        aot_cpu_features = '+vfp2'
    elif aot_target.startswith('x86_64'):
        aot_cpu = 'core2'

    wamrc_args = [
        'wamrc',
        '--target=' + aot_arch_target,
        '--target-abi=' + aot_abi,
        '--cpu=' + aot_cpu
    ]
    if aot_cpu_features:
        wamrc_args += ['--cpu-features=' + aot_cpu_features]

    wamrc_args += [
        '--bounds-checks=1',
        '--disable-simd',
        '-o', argv['o'],
        argv['INPUT_FILE']
    ]

    sys.exit(subprocess.run(wamrc_args).returncode)
