import sys
sys.path.append("../../../")

import os
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
  lat_list = {}
  out_dir = "./out/"
  for msize in parsed_md:
    data_point = {}
    for stack in parsed_md[msize]:
      latencies = putils.init_latencies()
      for run in parsed_md[msize][stack]:
        fname_c0 = out_dir + parsed_md[msize][stack][run]['0']['0']
        putils.add_latencies(latencies, fname_c0)
      
      putils.divide_latencies(latencies, len(parsed_md[msize][stack]))
      data_point[stack] = latencies
  
    lat_list[msize] = data_point
  
  return lat_list

def save_dat_file(exp_lats):
  header = "nconns bare-tas bare-vtas virt-tas ovs-linux ovs-tas\n"
  
  msizes = list(exp_lats.keys())
  msizes = list(map(str, sorted(map(int, msizes))))
  stacks =  list(exp_lats[msizes[0]].keys())
  percentiles =  list(exp_lats[msizes[0]][stacks[0]].keys())

  for percentile in percentiles:
      fname = "./lat_{}.dat".format(percentile)
      f = open(fname, "w+")
      f.write(header)

      for msize in msizes:
        f.write("{} {} {} {} {} {}\n".format(
          msize,
          exp_lats[msize]['bare-tas'][percentile],
          exp_lats[msize]['bare-vtas'][percentile],
          exp_lats[msize]['virt-tas'][percentile],
          exp_lats[msize]['ovs-linux'][percentile],
          exp_lats[msize]['ovs-tas'][percentile])
        )
        
def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()