import sys
sys.path.append("../../../")

import os
import re
import experiments.plot_utils as putils


# For this experiment get the message size
# from the experiment name, since client 0 and client 1
# have different message sizes
def get_msize(fname):
  regex = "(?<=_msize)[0-9]*"
  msize = re.search(regex, fname).group(0)
  return msize

def check_msize(data, msize):
  if msize not in data:
    data[msize] = {}

def check_stack(data, msize, stack):
  if stack not in data[msize]:
    data[msize][stack] = {}

def check_nid(data, msize, stack, nid):
  if nid not in data[msize][stack]:
    data[msize][stack][nid] = {}

def check_cid(data, msize, stack, nid, cid):
  if cid not in data[msize][stack][nid]:
    data[msize][stack][nid][cid] = ""

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

  return str((n_messages * msize * 8 / n) / 1000000)

def parse_metadata():
  dir_path = "./out/"
  data = {}

  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)

    if "tas_c" == fname:
      continue

    msize = get_msize(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_msize(data, msize)
    check_stack(data, msize, stack)
    check_nid(data, msize, stack, nid)
    check_cid(data, msize, stack, nid, cid)

    data[msize][stack][nid][cid] = fname

  return data

def parse_data(parsed_md):
  tp = []
  out_dir = "./out/"
  for msize in parsed_md:
    data_point = {"msize": msize}
    for stack in parsed_md[msize]:
      is_virt = stack == "virt-tas" or stack == "ovs-tas" or stack == "ovs-linux"
      if is_virt:
        c0_fname = out_dir + parsed_md[msize][stack]["0"]["0"]
        c1_fname = out_dir + parsed_md[msize][stack]["1"]["0"]
      else:
        c0_fname = out_dir + parsed_md[msize][stack]["0"]["0"]
        c1_fname = out_dir + parsed_md[msize][stack]["0"]["1"]

      avg_tp = get_avg_tp(c0_fname, c1_fname)

      data_point[stack] = avg_tp
  
    tp.append(data_point)
  
  tp = sorted(tp, key=lambda d: int(d['msize']))
  return tp

def save_dat_file(avg_tps, fname):
  f = open(fname, "w+")
  header = "msize bare-tas bare-vtas virt-tas ovs-linux ovs-tas\n"
  f.write(header)
  for tp in avg_tps:
    f.write("{} {} {} {}\n".format(
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