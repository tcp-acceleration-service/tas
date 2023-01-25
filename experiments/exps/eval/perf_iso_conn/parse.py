import os
import re

def get_stack(line):
  stack_regex = "[a-z]+-[a-z]+"
  stack = re.search(stack_regex, line).group(0)
  return stack

def get_client_id(line):
  cid_regex = "(?<=64_)[0-9]*"
  cid = re.search(cid_regex, line).group(0)
  return cid

def get_tp(line):
  tp_regex = "(?<=(total=))(.*?)(?=\ )"
  tp = re.search(tp_regex, line).group(0)
  return tp

def get_ts(line):
  ts_regex = "(?<=(ts=))(.*?)(?=\,)"
  ts = re.search(ts_regex, line).group(0)
  return ts

def get_nconns(fname):
  nconns_regex = "(?<=(conn_))(.*?)(?=\-)"
  num = re.search(nconns_regex, fname).group(0)
  return num

def get_first_ts(path):
  f = open(path)
  lines = f.readlines()

  l = lines[0]
  first_ts = get_ts(l)
  return first_ts

def get_last_ts(path):
  f = open(path)
  lines = f.readlines()

  l = lines[len(lines) - 1]
  last_ts = get_ts(l)
  return last_ts

def get_min_idx(path, c1_first_ts):
  f = open(path)

  for idx, l in enumerate(f):
    ts = get_ts(l)

    if int(ts) > int(c1_first_ts):
      return idx, ts

  return -1, -1

def get_avg_tp(path_c0, path_c1):
  tp_sum = 0
  n = 0

  f = open(path_c0)
  lines = f.readlines()

  c1_first_ts = get_last_ts(path_c1)
  idx, _ = get_min_idx(path_c0, c1_first_ts)


  for l in lines[idx:]:
    tp = get_tp(l)
    tp_sum += float(tp)
    n += 1

  return str(tp_sum / n)

def check_nconns(data, nconns):
  if nconns not in data:
    data[nconns] = {}

def check_stack(data, nconns, stack):
  if stack not in data[nconns]:
    data[nconns][stack] = {}

def check_cid(data, nconns, stack, cid):
  if cid not in data[nconns][stack]:
    data[nconns][stack][cid] = ""

def parse_metadata():
  dir_path = "./out/"
  data = {}

  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)
    nconns = get_nconns(fname)
    cid = get_client_id(fname)
    stack = get_stack(fname)

    check_nconns(data, nconns)
    check_stack(data, nconns, stack)
    check_cid(data, nconns, stack, cid)

    data[nconns][stack][cid] = fname

  return data

def parse_data(parsed_md):
  tp = []
  out_dir = "./out/"
  for nconns in parsed_md:
    data_point = {"nconns": nconns}
    for stack in parsed_md[nconns]:
      c0_fname = out_dir + parsed_md[nconns][stack]["0"]
      c1_fname = out_dir + parsed_md[nconns][stack]["1"]
      avg_tp = get_avg_tp(c0_fname, c1_fname)

      data_point[stack] = avg_tp
    
    tp.append(data_point)
  
  tp = sorted(tp, key=lambda d: int(d['nconns']))
  return tp

def save_dat_file(avg_tps, fpath):
  f = open(fpath, "w+")
  header = "nconns virt-linux virt-tas\n"
  f.write(header)

  for tp in avg_tps:
    f.write("{} {} {}\n".format(
        tp["nconns"], tp["virt-linux"], tp["virt-tas"]))

def main():
  parsed_md = parse_metadata()
  avg_tps = parse_data(parsed_md)
  save_dat_file(avg_tps, "./tp.dat")

if __name__ == '__main__':
  main()