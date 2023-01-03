
import argparse
import time
import os
import importlib
import importlib.util
import fnmatch

parser = argparse.ArgumentParser()
parser.add_argument('experiments', metavar='EXP', type=str, nargs='+',
        help='An experiment file to run')
parser.add_argument('--filter', metavar='PATTERN', type=str, nargs='+',
        help='Pattern to match experiment names against')
parser.add_argument('--force', action='store_const', const=True, default=False,
        help='Run experiments even if output already exists')
parser.add_argument('--verbose', action='store_const', const=True,
        default=False,
        help='Verbose output')
parser.add_argument('--reset', action='store_const', const=True,
        default=False,
        help="Close panes from previous run")

g_env = parser.add_argument_group('Environment')
g_env.add_argument('--workdir', metavar='DIR', type=str,
        default='./out/', help='Work directory base')

args = parser.parse_args()
experiments = []

for path in args.experiments:
    modname, _ = os.path.splitext(os.path.basename(path))
    print(modname)
    spec = importlib.util.spec_from_file_location(modname, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    experiments += mod.experiments

if (args.reset):
    for e in experiments:
        e.reset()
    exit()

for e in experiments:
    if (args.filter) and (len(args.filter) > 0):
        match = False
        for f in args.filter:
            if fnmatch.fnmatch(e.get_name(), f):
                match = True
                break
        if not match:
            continue
    print('******' + e.get_name() + '********')
    e.reset()
    e.run()
    print(e.get_name())
    import pdb
    pdb.set_trace()
    time.sleep(100)
    e.reset()