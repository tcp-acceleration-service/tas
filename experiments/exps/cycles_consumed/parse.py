import sys
sys.path.append("../../../")

import experiments.plot_utils as putils

def parse_latencies(fname):
  f = open(fname)
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
    "vm0_cycles_poll": [],
    "vm0_cycles_tx": [],
    "vm0_cycles_rx": [],
    "vm1_cycles_total": [],
    "vm1_cycles_rate": [],
    "vm1_cycles_poll": [],
    "vm1_cycles_tx": [],
    "vm1_cycles_rx": [],
  }

  for line in lines:
    if "VM" in line:
      timestamp = putils.get_ts(line)
      vm0tc = putils.get_cycles_total(line, 0)
      vm0rc = putils.get_cycles_rate(line, 0)
      vm0_cpoll = putils.get_cycles_poll(line, 0)
      vm0_ctx = putils.get_cycles_tx(line, 0)
      vm0_crx = putils.get_cycles_rx(line, 0)
      vm1tc = putils.get_cycles_total(line, 1)
      vm1rc = putils.get_cycles_rate(line, 1)
      vm1_cpoll = putils.get_cycles_poll(line, 1)
      vm1_ctx = putils.get_cycles_tx(line, 1)
      vm1_crx = putils.get_cycles_rx(line, 1)

      tas_data["timestamps"].append(timestamp)
      tas_data["vm0_cycles_total"].append(vm0tc)
      tas_data["vm0_cycles_rate"].append(vm0rc)
      tas_data["vm0_cycles_poll"].append(vm0_cpoll)
      tas_data["vm0_cycles_tx"].append(vm0_ctx)
      tas_data["vm0_cycles_rx"].append(vm0_crx)
      tas_data["vm1_cycles_total"].append(vm1tc)
      tas_data["vm1_cycles_rate"].append(vm1rc)
      tas_data["vm1_cycles_poll"].append(vm1_cpoll)
      tas_data["vm1_cycles_tx"].append(vm1_ctx)
      tas_data["vm1_cycles_rx"].append(vm1_crx)

  return tas_data

def save_cycles_phase_agg_dat(parsed_tas):
  header = "vmid ts cycles_poll cycles_tx cycles_rx\n"
  
  f = open("cycles_phase_agg.dat", "w+")
  f.write(header)

  vm0_poll_sum = 0
  vm0_tx_sum = 0
  vm0_rx_sum = 0
  vm1_poll_sum = 0
  vm1_tx_sum = 0
  vm1_rx_sum = 0

  ts_len = len(parsed_tas["timestamps"])
  total_ts = int(parsed_tas["timestamps"][ts_len - 1]) - int(parsed_tas["timestamps"][0])

  for i in range(len(parsed_tas["timestamps"])):
    vm0_poll_sum += int(parsed_tas["vm0_cycles_poll"][i])
    vm0_tx_sum += int(parsed_tas["vm0_cycles_tx"][i])
    vm0_rx_sum += int(parsed_tas["vm0_cycles_rx"][i])

    vm1_poll_sum += int(parsed_tas["vm1_cycles_poll"][i])
    vm1_tx_sum += int(parsed_tas["vm1_cycles_tx"][i])
    vm1_rx_sum += int(parsed_tas["vm1_cycles_rx"][i])

  f.write("{} {} {} {} {}\n".format(
    "vm0", total_ts,
    vm0_poll_sum, vm0_tx_sum, vm0_rx_sum
  ))

  f.write("{} {} {} {} {}\n".format(
    "vm1", total_ts,
    vm1_poll_sum, vm1_tx_sum, vm1_rx_sum
  ))

def save_cycles_dat(parsed_tas):
  header = "timestamp " + \
      "vm0_rate vm0_total vm0_cycles_poll vm0_cycles_tx vm0_cycles_rx " + \
      "vm1_rate vm1_total vm1_cycles_poll vm1_cycles_tx vm1_cycles_rx\n"
  f = open("cycles.dat", "w+")
  f.write(header)

  for i in range(len(parsed_tas["timestamps"])):
    ts = parsed_tas["timestamps"][i]
    vm0r = parsed_tas["vm0_cycles_rate"][i]
    vm0t = parsed_tas["vm0_cycles_total"][i]
    vm0_poll = parsed_tas["vm0_cycles_poll"][i]
    vm0_tx = parsed_tas["vm0_cycles_tx"][i]
    vm0_rx = parsed_tas["vm0_cycles_rx"][i]
    vm1r = parsed_tas["vm1_cycles_rate"][i]
    vm1t = parsed_tas["vm1_cycles_total"][i] 
    vm1_poll = parsed_tas["vm1_cycles_poll"][i]
    vm1_tx = parsed_tas["vm1_cycles_tx"][i]
    vm1_rx = parsed_tas["vm1_cycles_rx"][i]
    f.write("{} {} {} {} {} {} {} {} {} {} {}\n".format(
      ts, 
      vm0r, vm0t, vm0_poll, vm0_tx, vm0_rx,
      vm1r, vm1t, vm1_poll, vm1_tx, vm1_rx
    ))

def save_lat_dat(parsed_latencies, cid):
  header = "50p 90p 99p 99.9p 99.99p\n"
  f = open("client{}_lat.dat".format(cid), "w+")
  f.write(header)

  f.write("{} {} {} {} {}\n".format(
    parsed_latencies["50p"], parsed_latencies["90p"],
    parsed_latencies["99p"], parsed_latencies["99.9p"],
    parsed_latencies["99.99p"]
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
  latencies_c0 = parse_latencies("./out/cycles-consumed_bare-vtas_client0_node0_nconns512_ncores3_msize64")
  latencies_c1 = parse_latencies("./out/cycles-consumed_bare-vtas_client1_node0_nconns4096_ncores3_msize64")
  
  save_cycles_dat(parsed_tas)
  save_cycles_phase_agg_dat(parsed_tas)
  save_client_dat(parsed_c0, 0)
  save_client_dat(parsed_c1, 1)
  save_lat_dat(latencies_c0, 0)

if __name__ == '__main__':
  main()