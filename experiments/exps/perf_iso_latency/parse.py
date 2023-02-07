import sys
sys.path.append("../../../")

import os
import re
import functools
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
  lat_list = {}
  out_dir = "./out/"
  for nconns in parsed_md:
    data_point = {}
    for stack in parsed_md[nconns]:
      fname_c0 = out_dir + parsed_md[nconns][stack]['0']['0']
      latencies = get_latencies(fname_c0)
      data_point[stack] = latencies
  
    lat_list[nconns] = data_point
  
  return lat_list

def save_dat_file(exp_lats, fname):
  header = "nconns bare-tas bare-vtas virt-tas\n"
  
  nconns = list(exp_lats.keys())
  nconns = sorted(nconns) 
  stacks =  list(exp_lats[nconns[0]].keys())
  percentiles =  list(exp_lats[nconns[0]][stacks[0]].keys())

  for percentile in percentiles:
      fname = "./lat_{}.dat".format(percentile)
      f = open(fname, "w+")
      f.write(header)

      for nconn in nconns:
        f.write("{} {} {} {}\n".format(
          nconn,
          exp_lats[nconn]['bare-tas'][percentile],
          exp_lats[nconn]['bare-vtas'][percentile],
          exp_lats[nconn]['virt-tas'][percentile])
        )
        
def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies, "./lat.dat")

if __name__ == '__main__':
  main()