import sys
sys.path.append("../../../")

import os
import re
import functools
import experiments.plot_utils as putils


# For this experiment get the message size
# from the experiment name, since client 0 and client 1
# have a different message size
def get_msize(fname):
  regex = "(?<=-msize)[0-9]*"
  msize = re.search(regex, fname).group(0)
  return msize

def check_msize(data, msize):
  if msize not in data:
    data[msize] = {}

def check_stack(data, nconns, stack):
  if stack not in data[nconns]:
    data[nconns][stack] = {}

def check_nid(data, nconns, stack, nid):
  if nid not in data[nconns][stack]:
    data[nconns][stack][nid] = {}

def check_cid(data, nconns, stack, nid, cid):
  if cid not in data[nconns][stack][nid]:
    data[nconns][stack][nid][cid] = ""

def get_latencies(fname_c0):
  f = open(fname_c0)
  lines = f.readlines()

  # Latencies are already accumulated over all time
  # period in the logs
  line = lines[len(lines) - 1]
  latencies = {}
  latencies["50p"] = putils.get_50p_lat(line)
  latencies["90p"] = putils.get_90p_lat(line)
  latencies["99p"] = putils.get_99p_lat(line)
  latencies["99.9p"] = putils.get_99_9p_lat(line)
  latencies["99.99p"] = putils.get_99_99p_lat(line)

  return latencies

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
  lat_list = {}
  out_dir = "./out/"
  for msize in parsed_md:
    data_point = {}
    for stack in parsed_md[msize]:
      fname_c0 = out_dir + parsed_md[msize][stack]['0']['0']
      latencies = get_latencies(fname_c0)
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
          exp_lats[msize]['ovs-tas'][percentile],)
        )
        
def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()