import experiments as exp

from exps.overhead_throughput.configs.bare_tas import Config as TasBareConf
from exps.overhead_throughput.configs.bare_vtas import Config as VTasBareConf
from exps.overhead_throughput.configs.bare_vtas_tunoff import Config as VTasBareTunOffConf
from exps.overhead_throughput.configs.virt_tas import Config as TasVirtConf

experiments = []

msize = [64, 128, 256, 512, 1024, 2048]
n_runs = 5

for n_r in range(n_runs):
  for n_m in msize:
    exp_name = "overhead-throughput-run{}-msize{}_".format(n_r, n_m)
    tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas", n_m), name=exp_name)
    vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas", n_m), name=exp_name)
    vtas_bare_tunoff_exp = exp.Experiment(VTasBareTunOffConf(exp_name + "bare-tunoffvtas", n_m), name=exp_name)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_m), name=exp_name)

    experiments.append(tas_bare_exp)
    experiments.append(vtas_bare_tunoff_exp)
    experiments.append(vtas_bare_exp)
    experiments.append(tas_virt_exp)
  
