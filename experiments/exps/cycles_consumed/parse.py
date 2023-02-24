import sys
sys.path.append("../../../")

import experiments.plot_utils as putils

def parse_client(fname):
  f = open(fname)
  lines = f.readlines()

  client_data = {
    "timestamps": [],
    "nmsgs": [],
    "tps": []
  }

  for line in lines:
    ts = putils.get_ts(line)
    num_msgs = putils.get_n_messages(line)
    tp = putils.get_tp(line).replace(",", "")

    client_data["timestamps"].append(ts)
    client_data["nmsgs"].append(num_msgs)
    client_data["tps"].append(tp)

  return client_data

def parse_tas(fname):
  f = open(fname)
  lines = f.readlines()

  tas_data = {
    "timestamps": [],
    "vm0_cycles_total": [],
    "vm0_cycles_rate": [],
    "vm1_cycles_total": [],
    "vm1_cycles_rate": []
  }

  for line in lines:
    if "VM" in line:
      timestamp = putils.get_ts(line)
      vm0tc = putils.get_cycles_total(line, 0)
      vm0rc = putils.get_cycles_rate(line, 0)
      vm1tc = putils.get_cycles_total(line, 1)
      vm1rc = putils.get_cycles_rate(line, 1)

      tas_data["timestamps"].append(timestamp)
      tas_data["vm0_cycles_total"].append(vm0tc)
      tas_data["vm0_cycles_rate"].append(vm0rc)
      tas_data["vm1_cycles_total"].append(vm1tc)
      tas_data["vm1_cycles_rate"].append(vm1rc)

  return tas_data

def save_cycles_dat(parsed_tas):
  header = "timestamp vm0_rate vm0_total vm1_rate vm1_total\n"
  f = open("cycles.dat", "w+")
  f.write(header)

  for i in range(len(parsed_tas["timestamps"])):
    ts = parsed_tas["timestamps"][i]
    vm0r = parsed_tas["vm0_cycles_rate"][i]
    vm0t = parsed_tas["vm0_cycles_total"][i]
    vm1r = parsed_tas["vm1_cycles_rate"][i]
    vm1t = parsed_tas["vm1_cycles_total"][i] 
    f.write("{} {} {} {} {}\n".format(
      ts, vm0r, vm0t, vm1r, vm1t
    ))

def save_client_dat(parsed_client, cid):
  header = "timestamp n_messages tp\n"
  f = open("client{}.dat".format(cid), "w+")
  f.write(header)

  for i in range(len(parsed_client["timestamps"])):
    ts = parsed_client["timestamps"][i]
    nmsgs = parsed_client["nmsgs"][i]
    tp = parsed_client["tps"][i]
    f.write("{} {} {}\n".format(
      ts, nmsgs, tp
    ))

def main():
  parsed_c0 = parse_client("./out/cycles-consumed_bare-vtas_client0_node0_nconns512_ncores3_msize64")
  parsed_c1 = parse_client("./out/cycles-consumed_bare-vtas_client1_node0_nconns4096_ncores3_msize64")
  parsed_tas = parse_tas("./out/tas_c")
  
  save_cycles_dat(parsed_tas)
  save_client_dat(parsed_c0, 0)
  save_client_dat(parsed_c1, 1)

if __name__ == '__main__':
  main()