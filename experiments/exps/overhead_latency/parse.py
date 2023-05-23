import sys
sys.path.append("../../../")

import os
import experiments.plot_utils as putils


def check_stack(data, stack):
  if stack not in data:
    data[stack] = {}

def check_run(data, stack, run):
  if run not in data[stack]:
    data[stack][run] = {}

def check_nid(data, stack, run, nid):
  if nid not in data[stack][run]:
    data[stack][run][nid] = {}

def check_cid(data, stack, run, nid, cid):
  if cid not in data[stack][run][nid]:
    data[stack][run][nid][cid] = ""


def parse_metadata():
  dir_path = "./out/"
  data = {}

  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)

    if "tas_c" == fname:
      continue

    run = putils.get_expname_run(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_stack(data, stack)
    check_run(data, stack, run)
    check_nid(data, stack, run, nid)
    check_cid(data, stack, run, nid, cid)

    data[stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = {}
  out_dir = "./out/"
  for stack in parsed_md:
    latencies = putils.init_latencies()
    for run in parsed_md[stack]:
      fname_c0 = out_dir + parsed_md[stack][run]['0']['0']
      putils.append_latencies(latencies, fname_c0)

    data[stack] = {
      "lat": putils.get_latency_avg(latencies),
      "std": putils.get_latency_std(latencies)
    }
  
  return data

def save_dat_file(data):
  # stacks =  list(data.keys())
  stacks = ["bare-tas", "bare-vtas", "virt-tas", "bare-tunoffvtas"]

  header = "stack 50p-avg 90p-avg 99p-avg 99.9p-avg 99.99p-avg " + \
      "50p-std 90p-std 99p-std 99.9p-std 99.99p-std\n"
  fname = "./lat.dat"
  f_avg = open(fname, "w+")
  f_avg.write(header)

  for stack in stacks:
    f_avg.write("{} {} {} {} {} {} {} {} {} {} {}\n".format(
      stack,
      data[stack]["lat"]["50p"],
      data[stack]["lat"]["90p"],
      data[stack]["lat"]["99p"],
      data[stack]["lat"]["99.9p"],
      data[stack]["lat"]["99.99p"],      
      data[stack]["std"]["50p"],
      data[stack]["std"]["90p"],
      data[stack]["std"]["99p"],
      data[stack]["std"]["99.9p"],
      data[stack]["std"]["99.99p"]))

def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()