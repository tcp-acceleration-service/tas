import sys
sys.path.append("../../../")

import os
import numpy as np
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

def get_avg_tp(fname_c0):
  n_messages = 0
  n = 0

  f = open(fname_c0)
  lines = f.readlines()

  first_line = lines[0]
  last_line = lines[len(lines) - 1]

  n_messages = int(putils.get_n_messages(last_line)) - \
      int(putils.get_n_messages(first_line))
  msize = int(putils.get_msize(fname_c0))
  n = len(lines)

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
  data = []
  out_dir = "./out/"
  for msize in parsed_md:
    data_point = {"msize": msize}
    for stack in parsed_md[msize]:
      tp_x = np.array([])
      for run in parsed_md[msize][stack]:
        is_virt = stack == "virt-tas" or stack == "ovs-tas" or stack == "ovs-linux"
        if is_virt:
          c0_fname = out_dir + parsed_md[msize][stack][run]["0"]["0"]
        else:
          c0_fname = out_dir + parsed_md[msize][stack][run]["0"]["0"]

        tp = get_avg_tp(c0_fname)
        if tp > 0:
          tp_x = np.append(tp_x, tp)

      data_point[stack] = {
        "tp": tp_x.mean(),
        "std": tp_x.std(),
      }
  
    data.append(data_point)
  
  data = sorted(data, key=lambda d: int(d['msize']))
  return data

def save_dat_file(data, fname):
  f = open(fname, "w+")
  header = "msize " + \
      "bare-tas-avg bare-vtas-avg virt-tas-avg " + \
      "ovs-linux-avg " + \
      "bare-tas-std bare-vtas-std virt-tas-std " + \
      "ovs-linux-std\n"
  f.write(header)
  for dp in data:
    f.write("{} {} {} {} {} {} {}\n".format(
      dp["msize"],
      dp["bare-tas"]["tp"], dp["bare-vtas"]["tp"], dp["virt-tas"]["tp"],
      dp["bare-tas"]["std"], dp["bare-vtas"]["std"], dp["virt-tas"]["std"]))
        
def main():
  parsed_md = parse_metadata()
  data = parse_data(parsed_md)
  save_dat_file(data, "./tp.dat")

if __name__ == '__main__':
  main()