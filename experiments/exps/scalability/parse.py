import os
import experiments.plot_utils as putils

NUM_CORES = 8

def get_avg_tp(fname):
  tp_sum = 0
  n = 0

  f = open(fname)
  lines = f.readlines()

  for l in lines[:60]:
    tp = putils.get_tp(l)
    tp_sum += float(tp)
    n += 1

  return tp_sum / n

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
      agg_tp = 0
      for node in parsed_md[nconns][stack]:
        for client in parsed_md[nconns][stack][node]:
          fname = out_dir + parsed_md[nconns][stack][node][client]
          avt_tp = get_avg_tp(fname)
          agg_tp += avt_tp

      data_point[stack] = agg_tp
    
    tp.append(data_point)
  
  tp = sorted(tp, key=lambda d: int(d['nconns']))
  return tp

def save_dat_file(avg_tps, fpath):
  f = open(fpath, "w+")
  header = "nconns bare-linux virt-linux bare-tas virt-tas\n"
  f.write(header)
  for tp in avg_tps:
    f.write("{} {} {} {} {}\n".format(
        tp["nconns"], 
        tp["bare-linux"], tp["virt-linux"], 
        tp["bare-tas"], tp["virt-tas"]))

def main():
  parsed_md = parse_metadata()
  avg_tps = parse_data(parsed_md)
  save_dat_file(avg_tps, "./tp.dat")

if __name__ == '__main__':
  main()