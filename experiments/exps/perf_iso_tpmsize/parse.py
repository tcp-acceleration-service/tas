import sys
sys.path.append("../../../")

import os
import experiments.plot_utils as putils


def check_msize(data, msize):
  if msize not in data:
    data[msize] = {}

def check_stack(data, msize, stack):
  if stack not in data[msize]:
    data[msize][stack] = {}

def check_run(data, msize, stack, run):
  if run not in data[msize][stack]:
    data[msize][stack][run] = {}

def check_nid(data, msize, stack, run, nid):
  if nid not in data[msize][stack][run]:
    data[msize][stack][run][nid] = {}

def check_cid(data, msize, stack, run, nid, cid):
  if cid not in data[msize][stack][run][nid]:
    data[msize][stack][run][nid][cid] = ""

def get_avg_tp(fname_c0, fname_c1):
  n_messages = 0
  n = 0

  f = open(fname_c0)
  lines = f.readlines()

  c1_first_ts = putils.get_first_ts(fname_c1)
  idx, _ = putils.get_min_idx(fname_c0, c1_first_ts)

  first_line = lines[idx]
  last_line = lines[len(lines) - 1]

  n_messages = int(putils.get_n_messages(last_line)) - \
      int(putils.get_n_messages(first_line))
  msize = int(putils.get_msize(fname_c0))
  n = len(lines) - idx

  return (n_messages * msize * 8 / n) / 1000000

def parse_metadata():
  dir_path = "./out/"
  data = {}

  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)

    if "tas_c" == fname:
      continue

    run = putils.get_expname_run(fname)
    msize = putils.get_expname_msize(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_msize(data, msize)
    check_stack(data, msize, stack)
    check_run(data, msize, stack, run)
    check_nid(data, msize, stack, run, nid)
    check_cid(data, msize, stack, run, nid, cid)

    data[msize][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  tp = []
  out_dir = "./out/"
  for msize in parsed_md:
    data_point = {"msize": msize}
    for stack in parsed_md[msize]:
      avg_tp = 0
      for run in parsed_md[msize][stack]:
        is_virt = stack == "virt-tas" or stack == "ovs-tas" or stack == "ovs-linux"
        if is_virt:
          c0_fname = out_dir + parsed_md[msize][stack][run]["0"]["0"]
          c1_fname = out_dir + parsed_md[msize][stack][run]["1"]["0"]
        else:
          c0_fname = out_dir + parsed_md[msize][stack][run]["0"]["0"]
          c1_fname = out_dir + parsed_md[msize][stack][run]["0"]["1"]

        avg_tp += get_avg_tp(c0_fname, c1_fname)

      data_point[stack] = avg_tp / len(parsed_md[msize][stack])
  
    tp.append(data_point)
  
  tp = sorted(tp, key=lambda d: int(d['msize']))
  return tp

def save_dat_file(avg_tps, fname):
  f = open(fname, "w+")
  header = "msize bare-tas bare-vtas virt-tas ovs-linux ovs-tas\n"
  f.write(header)
  for tp in avg_tps:
    f.write("{} {} {} {} {} {}\n".format(
      tp["msize"],
      tp["bare-tas"],
      tp["bare-vtas"],
      tp["virt-tas"],
      tp["ovs-linux"],
      tp["ovs-tas"]
    ))
        
def main():
  parsed_md = parse_metadata()
  avg_tps = parse_data(parsed_md)
  save_dat_file(avg_tps, "./tp.dat")

if __name__ == '__main__':
  main()