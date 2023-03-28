import sys
sys.path.append("../../../")

import os
import re
import experiments.plot_utils as putils


# For this experiment get the number of connections
# from the experiment name, since client 0 and client 1
# have a different number of connections
def get_conns(fname):
  regex = "(?<=_conns)[0-9]*"
  nconns = re.search(regex, fname).group(0)
  return nconns

def check_nconns(data, nconns):
  if nconns not in data:
    data[nconns] = {}

def check_stack(data, nconns, stack):
  if stack not in data[nconns]:
    data[nconns][stack] = {}

def check_nid(data, nconns, stack, nid):
  if nid not in data[nconns][stack]:
    data[nconns][stack][nid] = {}

def check_cid(data, nconns, stack, nid, cid):
  if cid not in data[nconns][stack][nid]:
    data[nconns][stack][nid][cid] = ""

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

    nconns = get_conns(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_nconns(data, nconns)
    check_stack(data, nconns, stack)
    check_nid(data, nconns, stack, nid)
    check_cid(data, nconns, stack, nid, cid)

    data[nconns][stack][nid][cid] = fname

  return data

def parse_data(parsed_md):
  tp = []
  out_dir = "./out/"
  for nconns in parsed_md:
    data_point = {"nconns": nconns}
    for stack in parsed_md[nconns]:
      is_virt = stack == "virt-tas" or stack == "ovs-tas" or stack == "ovs-linux"
      if is_virt:
        c0_fname = out_dir + parsed_md[nconns][stack]["0"]["0"]
        c1_fname = out_dir + parsed_md[nconns][stack]["1"]["0"]
      else:
        c0_fname = out_dir + parsed_md[nconns][stack]["0"]["0"]
        c1_fname = out_dir + parsed_md[nconns][stack]["0"]["1"]

      avg_tp = get_avg_tp(c0_fname, c1_fname)

      data_point[stack] = avg_tp
  
    tp.append(data_point)
  
  tp = sorted(tp, key=lambda d: int(d['nconns']))
  return tp

def save_dat_file(avg_tps, fname):
  f = open(fname, "w+")
  header = "nconns bare-tas bare-vtas virt-tas ovs-linux ovs-tas\n"
  f.write(header)
  for tp in avg_tps:
    f.write("{} {} {} {}\n".format(
      tp["nconns"],
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