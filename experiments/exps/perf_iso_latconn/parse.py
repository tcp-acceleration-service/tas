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
  lat_list = {}
  out_dir = "./out/"
  for nconns in parsed_md:
    data_point = {}
    for stack in parsed_md[nconns]:
      latencies = putils.init_latencies()
      for run in parsed_md[nconns][stack]:
        fname_c0 = out_dir + parsed_md[nconns][stack][run]['0']['0']
        putils.add_latencies(latencies, fname_c0)
      
      putils.divide_latencies(latencies, len(parsed_md[nconns][stack]))
      data_point[stack] = latencies
  
    lat_list[nconns] = data_point
  
  return lat_list

def save_dat_file(exp_lats):
  header = "nconns bare-tas bare-vtas virt-tas ovs-linux ovs-tas\n"
  
  nconns = list(exp_lats.keys())
  nconns = list(map(str, sorted(map(int, nconns))))
  stacks =  list(exp_lats[nconns[0]].keys())
  percentiles =  list(exp_lats[nconns[0]][stacks[0]].keys())

  for percentile in percentiles:
      fname = "./lat_{}.dat".format(percentile)
      f = open(fname, "w+")
      f.write(header)

      for nconn in nconns:
        f.write("{} {} {} {} {} {}\n".format(
          nconn,
          exp_lats[nconn]['bare-tas'][percentile],
          exp_lats[nconn]['bare-vtas'][percentile],
          exp_lats[nconn]['virt-tas'][percentile],
          exp_lats[nconn]['ovs-linux'][percentile],
          exp_lats[nconn]['ovs-tas'][percentile])
        )
        
def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()