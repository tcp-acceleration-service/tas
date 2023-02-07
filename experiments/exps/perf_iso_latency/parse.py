import os
import experiments.plot_utils as putils


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

def get_latencies(fname_c0, fname_c1):
  f = open(fname_c0)
  lines = f.readlines()

  c1_first_ts = putils.get_last_ts(fname_c1)
  idx, _ = putils.get_min_idx(fname_c0, c1_first_ts)

  # Latencies are already accumulated over all time
  # period in the logs
  line = lines[idx]
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
    nconns = putils.get_nconns(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_nconns(data, nconns)
    check_stack(data, nconns, stack)
    check_nid(data, stack, nid)
    check_cid(data, stack, nid, cid)

    data[nconns][stack][nid][cid] = fname

  return data

def parse_data(parsed_md):
  lat_list = []
  out_dir = "./out/"
  for nconns in parsed_md:
    data_point = {"nconns": nconns}
    for stack in parsed_md[nconns]:
      
      if stack == "virt-tas":
        fname_c0 = out_dir + parsed_md[nconns][stack][0][0]
        fname_c1 = out_dir + parsed_md[nconns][stack][1][0]
      else:
        fname_c0 = out_dir + parsed_md[nconns][stack][0][0]
        fname_c1 = out_dir + parsed_md[nconns][stack][0][1]

      latencies = get_latencies(fname_c0, fname_c1)
      data_point[stack] = latencies
    
    lat_list.append(data_point)
  
  lat_list = sorted(lat_list, key=lambda d: int(d['nconns']))
  return lat_list

def save_dat_file(avg_tps, fpath):
  f = open(fpath, "w+")
  header = "nconns bare-tas bare-vtas virt-tas\n"
  f.write(header)
  for tp in avg_tps:
    f.write("{} {} {} {}\n".format(
        tp["nconns"], 
        tp["bare-tas"], tp["bare-vtas"], 
        tp["virt-tas"]))

def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies, "./tp.dat")

if __name__ == '__main__':
  main()