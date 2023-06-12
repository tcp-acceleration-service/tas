import numpy as np
import re

def init_latencies():
  latencies = {
    "50p": np.array([]),
    "90p": np.array([]),
    "99p": np.array([]),
    "99.9p": np.array([]),
    "99.99p": np.array([])
  }

  return latencies

def append_latencies(latencies, fname_c0):
  f = open(fname_c0)
  lines = f.readlines()

  # Latencies are already accumulated over all time
  # period in the logs
  line = lines[len(lines) - 1]

  lat = int(get_50p_lat(line))
  if lat > 0:
    latencies["50p"] = np.append(latencies["50p"], lat)

  lat = int(get_90p_lat(line))
  if lat > 0:
    latencies["90p"] = np.append(latencies["90p"], lat)

  lat = int(get_99p_lat(line))
  if lat > 0:
    latencies["99p"] = np.append(latencies["99p"], lat)

  lat = int(get_99_9p_lat(line))
  if lat > 0:
    latencies["99.9p"] = np.append(latencies["99.9p"], lat)

  lat = int(get_99_99p_lat(line))
  if lat > 0:
    latencies["99.99p"] = np.append(latencies["99.99p"], lat)

def get_lat_bar_size(avg_latencies):
  lat_diffs = {
    "50p": avg_latencies["50p"],
    "90p": avg_latencies["90p"] - avg_latencies["50p"],
    "99p": avg_latencies["99p"] - avg_latencies["90p"],
    "99.9p": avg_latencies["99.9p"] - avg_latencies["99p"],
    "99.99p": avg_latencies["99.99p"] - avg_latencies["99.9p"]
  }

  return lat_diffs

def get_latency_avg(latencies):
  avg_lats = {
    "50p": latencies["50p"].mean(),
    "90p": latencies["90p"].mean(),
    "99p": latencies["99p"].mean(),
    "99.9p": latencies["99.9p"].mean(),
    "99.99p": latencies["99.99p"].mean()
  }
  
  return avg_lats 

def get_latency_std(latencies):
  return {
    "50p": latencies["50p"].std(),
    "90p": latencies["90p"].std(),
    "99p": latencies["99p"].std(),
    "99.9p": latencies["99.9p"].std(),
    "99.99p": latencies["99.99p"].std()
  }

def get_expname_msize(fname):
  regex = "(?<=-msize)[0-9]*"
  msize = re.search(regex, fname).group(0)
  return msize

def get_expname_run(fname):
  run_id_regex = "(?<=-run)[0-9]*"
  run_id = re.search(run_id_regex, fname).group(0)
  return run_id

def get_expname_conns(fname):
  regex = "(?<=-conns)[0-9]*"
  nconns = re.search(regex, fname).group(0)
  return nconns

def get_stack(line):
  stack_regex = "(?<=_)([a-z]+-[a-z]+)(?=_)"
  stack = re.search(stack_regex, line).group(0)
  return stack

def get_client_id(line):
  cid_regex = "(?<=_client)[0-9]*"
  cid = re.search(cid_regex, line).group(0)
  return cid

def get_node_id(line):
  nid_regex = "(?<=_node)[0-9]*"
  nid = re.search(nid_regex, line).group(0)
  return nid

def get_nconns(line):
  nconns_regex = "(?<=_nconns)[0-9]*"
  num = re.search(nconns_regex, line).group(0)
  return num

def get_msize(line):
  msize_regex = "(?<=_msize)[0-9]*"
  msize = re.search(msize_regex, line).group(0)
  return msize

def get_n_messages(line):
  nmessages_regex = "(?<=(n_messages=))(.*?)(?=\ )"
  n_messages = re.search(nmessages_regex, line).group(0)
  return n_messages

def get_cycles_total(line, vmid):
  regex = "(?<=(TVM{}=))(.*?)(?=[\ \n])".format(vmid)
  cycles = re.search(regex, line).group(0)
  return cycles

def get_cycles_rate(line, vmid):
  regex = "(?<=(RVM{}=))(.*?)(?=[\ \n])".format(vmid)
  cycles = re.search(regex, line).group(0)
  return cycles

def get_budget(line, vmid):
  regex = "(?<=(BUVM{}=))(.*?)(?=[\ \n])".format(vmid)
  cycles = re.search(regex, line).group(0)
  return cycles

def get_cycles_poll(line, vmid):
  regex = "(?<=(POLLVM{}=))(.*?)(?=[\ \n])".format(vmid)
  cycles = re.search(regex, line).group(0)
  return cycles

def get_cycles_tx(line, vmid):
  regex = "(?<=(TXVM{}=))(.*?)(?=[\ \n])".format(vmid)
  cycles = re.search(regex, line).group(0)
  return cycles

def get_cycles_rx(line, vmid):
  regex = "(?<=(RXVM{}=))(.*?)(?=[\ \n])".format(vmid)
  cycles = re.search(regex, line).group(0)
  return cycles

def get_50p_lat(line):
  regex = "(?<=(50p=))(.*?)(?=\ )"
  lat = re.search(regex, line).group(0)
  return lat

def get_90p_lat(line):
  regex = "(?<=(90p=))(.*?)(?=\ )"
  lat = re.search(regex, line).group(0)
  return lat

def get_95p_lat(line):
  regex = "(?<=(95p=))(.*?)(?=\ )"
  lat = re.search(regex, line).group(0)
  return lat

def get_99p_lat(line):
  regex = "(?<=(99p=))(.*?)(?=\ )"
  lat = re.search(regex, line).group(0)
  return lat

def get_99_9p_lat(line):
  regex = "(?<=(99\.9p=))(.*?)(?=\ )"
  lat = re.search(regex, line).group(0)
  return lat

def get_99_99p_lat(line):
  regex = "(?<=(99\.99p=))(.*?)(?=\ )"
  lat = re.search(regex, line).group(0)
  return lat

def get_tp(line):
  tp_regex = "(?<=(total=))(.*?)(?=\ )"
  tp = re.search(tp_regex, line).group(0)
  return tp

def get_ts(line):
  ts_regex = "(?<=(ts=))(.*?)(?=\ )"
  ts = re.search(ts_regex, line).group(0)
  return ts

def get_elapsed(line):
  ts_regex = "(?<=(elapsed=))(.*?)(?=\ )"
  ts = re.search(ts_regex, line).group(0)
  return ts

def get_first_ts(fname):
  f = open(fname)
  lines = f.readlines()

  l = lines[0]
  first_ts = get_ts(l)
  return first_ts

def get_last_ts(fname):
  f = open(fname)
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
