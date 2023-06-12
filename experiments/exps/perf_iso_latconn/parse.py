import sys
sys.path.append("../../../")

import os
import experiments.plot_utils as putils


def check_nconns(data, nconns):
  if nconns not in data:
    data[nconns] = {}

def check_stack(data, nconns, stack):
  if stack not in data[nconns]:
    data[nconns][stack] = {}

def check_run(data, nconns, stack, run):
  if run not in data[nconns][stack]:
    data[nconns][stack][run] = {}

def check_nid(data, nconns, stack, run, nid):
  if nid not in data[nconns][stack][run]:
    data[nconns][stack][run][nid] = {}

def check_cid(data, nconns, stack, run, nid, cid):
  if cid not in data[nconns][stack][run][nid]:
    data[nconns][stack][run][nid][cid] = ""


def parse_metadata():
  dir_path = "./out/"
  data = {}

  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)

    if "tas_c" == fname:
      continue

    run = putils.get_expname_run(fname)
    nconns = putils.get_expname_conns(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_nconns(data, nconns)
    check_stack(data, nconns, stack)
    check_run(data, nconns, stack, run)
    check_nid(data, nconns, stack, run, nid)
    check_cid(data, nconns, stack, run, nid, cid)

    data[nconns][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = {}
  out_dir = "./out/"
  for nconn in parsed_md:
    data_point = {}
    for stack in parsed_md[nconn]:
      latencies = putils.init_latencies()
      for run in parsed_md[nconn][stack]:
        fname_c0 = out_dir + parsed_md[nconn][stack][run]['0']['0']
        putils.append_latencies(latencies, fname_c0)

      data_point[stack] = {
        "lat": putils.get_latency_avg(latencies),
        "std": putils.get_latency_std(latencies)
      }
    data[nconn] = data_point
  
  return data

def save_dat_file(data):
  header = "nconns " + \
      "bare-tas-avg bare-vtas-avg virt-tas-avg " + \
      "ovs-linux-avg " + \
      "bare-tas-std bare-vtas-std virt-tas-std " + \
      "ovs-linux-std\n"
  
  nconns = list(data.keys())
  nconns = list(map(str, sorted(map(int, nconns))))
  stacks =  list(data[nconns[0]].keys())
  percentiles =  list(data[nconns[0]][stacks[0]]['lat'].keys())

  for percentile in percentiles:
      fname = "./lat_{}.dat".format(percentile)
      f = open(fname, "w+")
      f.write(header)

      for nconn in nconns:
        f.write("{} {} {} {} {}\n".format(
          int(nconn),
          data[nconn]['bare-vtas']["lat"][percentile],
          data[nconn]['virt-tas']["lat"][percentile],
          data[nconn]['bare-vtas']["std"][percentile],
          data[nconn]['virt-tas']["std"][percentile]))
        
def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()