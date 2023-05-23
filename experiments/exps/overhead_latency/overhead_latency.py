import experiments as exp

from exps.overhead_latency.configs.bare_tas import Config as TasBareConf
from exps.overhead_latency.configs.bare_vtas import Config as VTasBareConf
from exps.overhead_latency.configs.bare_vtas_tunoff import Config as VTasBareTunOffConf
from exps.overhead_latency.configs.virt_tas import Config as TasVirtConf

experiments = []

n_runs = 5

for n_r in range(n_runs):
  exp_name = "overhead-latency-run{}_".format(n_r)
  tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas"), name=exp_name)
  vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas"), name=exp_name)
  vtas_bare_tunoff_exp = exp.Experiment(VTasBareTunOffConf(exp_name + "bare-tunoffvtas"), name=exp_name)
  tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas"), name=exp_name)

  experiments.append(tas_bare_exp)
  experiments.append(vtas_bare_tunoff_exp)
  experiments.append(vtas_bare_exp)
  experiments.append(tas_virt_exp)
  
