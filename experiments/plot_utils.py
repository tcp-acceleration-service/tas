import re

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
