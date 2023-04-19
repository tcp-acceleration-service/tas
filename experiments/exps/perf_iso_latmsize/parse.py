import sys
sys.path.append("../../../")

import os
import numpy as np
import experiments.plot_utils as putils


def check_msize(data, msize):
  if msize not in data:
    data[msize] = {}

def check_stack(data, nconns, stack):
  if stack not in data[nconns]:
    data[nconns][stack] = {}

def check_run(data, msize, stack, run):
  if run not in data[msize][stack]:
    data[msize][stack][run] = {}

def check_nid(data, msize, stack, run, nid):
  if nid not in data[msize][stack][run]:
    data[msize][stack][run][nid] = {}

def check_cid(data, msize, stack, run, nid, cid):
  if cid not in data[msize][stack][run][nid]:
    data[msize][stack][run][nid][cid] = ""

def parse_metadata():
  dir_path = "./out/"
  data = {}

  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)
    
    if "tas_c" == fname:
      continue
    
    run = putils.get_expname_run(fname)
    msize = putils.get_expname_msize(fname)
    nid = putils.get_node_id(fname)
    cid = putils.get_client_id(fname)
    stack = putils.get_stack(fname)

    check_msize(data, msize)
    check_stack(data, msize, stack)
    check_run(data, msize, stack, run)
    check_nid(data, msize, stack, run, nid)
    check_cid(data, msize, stack, run, nid, cid)

    data[msize][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = {}
  out_dir = "./out/"
  for msize in parsed_md:
    data_point = {}
    for stack in parsed_md[msize]:
      latencies = putils.init_latencies()
      for run in parsed_md[msize][stack]:
        fname_c0 = out_dir + parsed_md[msize][stack][run]['0']['0']
        putils.append_latencies(latencies, fname_c0)

      data_point[stack] = {
        "lat": putils.get_latency_avg(latencies),
        "std": putils.get_latency_std(latencies)
      }
    data[msize] = data_point
  
  return data

def save_dat_file(data):
  header = "msize " + \
      "bare-tas-avg bare-vtas-avg virt-tas-avg " + \
      "ovs-linux-avg " + \
      "bare-tas-std bare-vtas-std virt-tas-std " + \
      "ovs-linux-std\n"
  
  msizes = list(data.keys())
  msizes = list(map(str, sorted(map(int, msizes))))
  stacks =  list(data[msizes[0]].keys())
  percentiles =  list(data[msizes[0]][stacks[0]]['lat'].keys())

  for percentile in percentiles:
      fname = "./lat_{}.dat".format(percentile)
      f = open(fname, "w+")
      f.write(header)

      for msize in msizes:
        f.write("{} {} {} {} {} {} {} {} {}\n".format(
          msize,
          data[msize]['bare-tas']['lat'][percentile],
          data[msize]['bare-vtas']["lat"][percentile],
          data[msize]['virt-tas']["lat"][percentile],
          data[msize]['ovs-linux']["lat"][percentile],
          data[msize]['bare-tas']["std"][percentile],
          data[msize]['bare-vtas']["std"][percentile],
          data[msize]['virt-tas']["std"][percentile],
          data[msize]['ovs-linux']["std"][percentile]))

        
def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()